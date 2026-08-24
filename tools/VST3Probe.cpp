// -----------------------------------------------------------------------------
//  Loads the built .vst3 exactly the way a host does - CFBundle, bundleEntry,
//  GetPluginFactory - and prints what the factory advertises. If this passes,
//  the bundle is well formed and a DAW can see the plugin.
//
//  Deliberately uses only the VST3 interface headers, so it exercises the real
//  bundle rather than anything linked from this project.
// -----------------------------------------------------------------------------

#include <CoreFoundation/CoreFoundation.h>
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"   // kVstAudioEffectClass
#include <cstdio>
#include <cstring>

using namespace Steinberg;

using BundleEntryProc = bool (*) (CFBundleRef);
using BundleExitProc  = bool (*) ();
using GetFactoryProc  = IPluginFactory* (*) ();

static int fail (const char* message)
{
    std::printf ("FAIL: %s\n", message);
    return 1;
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return fail ("usage: vst3probe <path to .vst3>");

    const char* path = argv[1];

    auto url = CFURLCreateFromFileSystemRepresentation (
        nullptr, (const UInt8*) path, (CFIndex) std::strlen (path), true);
    if (url == nullptr)
        return fail ("could not make a URL for that path");

    auto bundle = CFBundleCreate (nullptr, url);
    CFRelease (url);
    if (bundle == nullptr)
        return fail ("CFBundleCreate failed - the bundle layout is wrong");

    auto entry = (BundleEntryProc) CFBundleGetFunctionPointerForName (bundle, CFSTR ("bundleEntry"));
    if (entry == nullptr)
        return fail ("bundleEntry not exported");

    if (! entry (bundle))
        return fail ("bundleEntry returned false");

    auto getFactory = (GetFactoryProc) CFBundleGetFunctionPointerForName (bundle, CFSTR ("GetPluginFactory"));
    if (getFactory == nullptr)
        return fail ("GetPluginFactory not exported");

    IPluginFactory* factory = getFactory();
    if (factory == nullptr)
        return fail ("GetPluginFactory returned null");

    PFactoryInfo info {};
    factory->getFactoryInfo (&info);
    std::printf ("  vendor     : %s\n", info.vendor);
    std::printf ("  url        : %s\n", info.url);

    const int32 numClasses = factory->countClasses();
    std::printf ("  classes    : %d\n", numClasses);
    if (numClasses <= 0)
        return fail ("factory exposes no classes");

    bool foundAudioModule = false;

    for (int32 i = 0; i < numClasses; ++i)
    {
        PClassInfo classInfo {};
        if (factory->getClassInfo (i, &classInfo) != kResultOk)
            return fail ("getClassInfo failed");

        std::printf ("    [%d] %-24s category=%s\n", i, classInfo.name, classInfo.category);

        // The class ID is what a host stores in a session to find this plug-in
        // again. It is derived from the manufacturer and plug-in codes, so it
        // is also the check that a second build system - the CMake one used for
        // Windows - produces the SAME plug-in and not a stranger that happens
        // to share a name.
        std::printf ("         cid        = ");
        for (int b = 0; b < 16; ++b)
            std::printf ("%02X", (unsigned char) classInfo.cid[b]);
        std::printf ("\n");

        if (std::strcmp (classInfo.category, kVstAudioEffectClass) == 0)
        {
            foundAudioModule = true;

            // Actually instantiate the processor component and initialise it -
            // this is where a broken plugin usually falls over.
            // Use the TUID constant rather than IComponent::iid: the latter only
            // exists when the SDK is built with the matching DEF_CLASS_IID.
            Vst::IComponent* component = nullptr;
            if (factory->createInstance (classInfo.cid, Vst::IComponent_iid,
                                         (void**) &component) != kResultOk
                || component == nullptr)
                return fail ("createInstance failed for the audio class");

            if (component->initialize (nullptr) != kResultOk)
                return fail ("IComponent::initialize failed");

            const int32 audioIns  = component->getBusCount (Vst::kAudio, Vst::kInput);
            const int32 audioOuts = component->getBusCount (Vst::kAudio, Vst::kOutput);
            const int32 eventIns  = component->getBusCount (Vst::kEvent, Vst::kInput);
            std::printf ("         audio in buses=%d, audio out buses=%d, event in buses=%d\n",
                         audioIns, audioOuts, eventIns);

            // MasterGlo Pro is an effect: it needs audio in and out, and it must
            // NOT advertise a MIDI input. This probe was originally written for
            // a synth, where the opposite was true - keeping the synth's
            // assertion here would fail a correctly built effect.
            if (audioIns  < 1) return fail ("no audio input bus - an effect would get no signal");
            if (audioOuts < 1) return fail ("no audio output bus");
            if (eventIns  > 0) return fail ("advertises a MIDI input bus, but this is an effect");

            component->terminate();
            component->release();
        }
    }

    if (! foundAudioModule)
        return fail ("no audio effect class in the factory");

    factory->release();

    if (auto exitProc = (BundleExitProc) CFBundleGetFunctionPointerForName (bundle, CFSTR ("bundleExit")))
        exitProc();

    std::printf ("PASS: VST3 bundle loads, factory instantiates, buses look right\n");
    return 0;
}
