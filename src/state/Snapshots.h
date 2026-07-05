#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sauce::state
{
    // =========================================================================
    // A/B/C/D snapshots. Each slot stores a full parameter state; slots are
    // persisted inside the plugin state so they survive session reload.
    // Capture/recall happen on the message thread (parameter setters are
    // thread-safe); the audio thread never touches this class.
    // =========================================================================
    class Snapshots
    {
    public:
        static constexpr int numSlots = 4;

        explicit Snapshots (juce::AudioProcessorValueTreeState& stateRef)
            : apvts (stateRef) {}

        /** Store the current parameters into a slot. */
        void capture (int slot)
        {
            if (juce::isPositiveAndBelow (slot, numSlots))
                slots[slot] = apvts.copyState().createCopy();
        }

        /** Recall a slot (no-op if the slot is empty). Records to undo. */
        void recall (int slot)
        {
            if (! juce::isPositiveAndBelow (slot, numSlots) || ! slots[slot].isValid())
                return;

            if (auto* um = apvts.undoManager)
                um->beginNewTransaction ("Recall snapshot " + juce::String (char ('A' + slot)));

            apvts.replaceState (slots[slot].createCopy());
            active = slot;
        }

        /** Copy the current parameters into a slot without switching to it. */
        void copyCurrentTo (int slot) { capture (slot); }

        void setActive (int slot)     { active = juce::jlimit (0, numSlots - 1, slot); }
        int  getActive() const        { return active; }
        bool hasData (int slot) const { return juce::isPositiveAndBelow (slot, numSlots) && slots[slot].isValid(); }

        /** Serialise all slots under 'parent'. */
        void writeTo (juce::ValueTree& parent) const
        {
            juce::ValueTree node ("SNAPSHOTS");
            node.setProperty ("active", active, nullptr);

            for (int i = 0; i < numSlots; ++i)
                if (slots[i].isValid())
                {
                    juce::ValueTree slotNode ("SLOT");
                    slotNode.setProperty ("index", i, nullptr);
                    slotNode.appendChild (slots[i].createCopy(), nullptr);
                    node.appendChild (slotNode, nullptr);
                }

            parent.appendChild (node, nullptr);
        }

        /** Restore slots from 'parent' (written by writeTo). */
        void readFrom (const juce::ValueTree& parent)
        {
            auto node = parent.getChildWithName ("SNAPSHOTS");
            if (! node.isValid())
                return;

            active = (int) node.getProperty ("active", 0);

            for (auto slotNode : node)
            {
                const int index = (int) slotNode.getProperty ("index", -1);
                if (juce::isPositiveAndBelow (index, numSlots) && slotNode.getNumChildren() > 0)
                    slots[index] = slotNode.getChild (0).createCopy();
            }
        }

    private:
        juce::AudioProcessorValueTreeState& apvts;
        juce::ValueTree slots[numSlots];
        int active = 0;
    };
} // namespace sauce::state
