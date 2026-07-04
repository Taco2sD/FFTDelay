#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

// A small on-disk preset system: each preset is the processor's full saved state
// (parameters + painted matrices) written to a file under the user's documents.
// Provides save / load plus Next / Previous navigation — the latter are the
// actions that must become MIDI-learnable (see the MIDI Learn layer).
//
// All methods touch the filesystem and call get/setStateInformation, so they are
// for the message thread only. MIDI-triggered navigation must be marshalled to
// the message thread by the caller (it is not realtime safe).
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessor& p) : processor (p)
    {
        presetDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                        .getChildFile ("FFTDelay").getChildFile ("Presets");
        presetDir.createDirectory();
        refresh();
    }

    void refresh()
    {
        presets.clear();
        for (auto& f : presetDir.findChildFiles (juce::File::findFiles, false, "*.preset"))
            presets.add (f.getFileNameWithoutExtension());
        presets.sortNatural();
    }

    const juce::StringArray& getPresets() const noexcept { return presets; }
    int  getCurrentIndex() const noexcept { return currentIndex; }
    juce::String getCurrentName() const
    {
        return juce::isPositiveAndBelow (currentIndex, presets.size()) ? presets[currentIndex]
                                                                       : juce::String ("Init");
    }

    void savePreset (const juce::String& name)
    {
        if (name.isEmpty())
            return;

        juce::MemoryBlock mb;
        processor.getStateInformation (mb);
        presetDir.getChildFile (name + ".preset").replaceWithData (mb.getData(), mb.getSize());

        refresh();
        currentIndex = presets.indexOf (name);
    }

    void loadPreset (const juce::String& name)
    {
        auto file = presetDir.getChildFile (name + ".preset");
        if (! file.existsAsFile())
            return;

        juce::MemoryBlock mb;
        if (file.loadFileAsData (mb))
        {
            processor.setStateInformation (mb.getData(), (int) mb.getSize());
            currentIndex = presets.indexOf (name);
            if (onPresetLoaded)
                onPresetLoaded();
        }
    }

    // Fired after a preset is recalled (used to optionally clear delay memory).
    std::function<void()> onPresetLoaded;

    void loadIndex (int i)
    {
        if (juce::isPositiveAndBelow (i, presets.size()))
            loadPreset (presets[i]);
    }

    void next()     { if (! presets.isEmpty()) loadIndex ((currentIndex + 1) % presets.size()); }
    void previous() { if (! presets.isEmpty()) loadIndex ((currentIndex - 1 + presets.size()) % presets.size()); }

private:
    juce::AudioProcessor& processor;
    juce::File            presetDir;
    juce::StringArray     presets;
    int                   currentIndex = -1;
};
