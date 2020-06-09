/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion_engine
{

//==============================================================================
//==============================================================================
FadeInOutNode::FadeInOutNode (std::unique_ptr<tracktion_graph::Node> inputNode,
                              tracktion_graph::PlayHeadState& playHeadStateToUse,
                              EditTimeRange in, EditTimeRange out,
                              AudioFadeCurve::Type fadeInType_, AudioFadeCurve::Type fadeOutType_,
                              bool clearSamplesOutsideFade)
    : input (std::move (inputNode)),
      playHeadState (playHeadStateToUse),
      fadeIn (in),
      fadeOut (out),
      fadeInType (fadeInType_),
      fadeOutType (fadeOutType_),
      clearExtraSamples (clearSamplesOutsideFade)
{
    jassert (! (fadeIn.isEmpty() && fadeOut.isEmpty()));
}

//==============================================================================
tracktion_graph::NodeProperties FadeInOutNode::getNodeProperties()
{
    auto props = input->getNodeProperties();
    props.nodeID = 0;

    return props;
}

std::vector<tracktion_graph::Node*> FadeInOutNode::getDirectInputNodes()
{
    return { input.get() };
}

void FadeInOutNode::prepareToPlay (const tracktion_graph::PlaybackInitialisationInfo& info)
{
    fadeInSampleRange = tracktion_graph::timeToSample (fadeIn, info.sampleRate);
    fadeOutSampleRange = tracktion_graph::timeToSample (fadeOut, info.sampleRate);
}

bool FadeInOutNode::isReadyToProcess()
{
    return input->hasProcessed();
}

void FadeInOutNode::process (const ProcessContext& pc)
{
    const auto timelineRange = referenceSampleRangeToSplitTimelineRange (playHeadState.playHead, pc.referenceSampleRange).timelineRange1;
    
    auto sourceBuffers = input->getProcessedOutput();
    auto destAudioBlock = pc.buffers.audio;
    auto destMidiBlock = pc.buffers.midi;
    jassert (sourceBuffers.audio.getNumChannels() == destAudioBlock.getNumChannels());

    destMidiBlock.copyFrom (sourceBuffers.midi);
    destAudioBlock.copyFrom (sourceBuffers.audio);

    if (! renderingNeeded (timelineRange))
        return;
    
    const int numSamples = (int) destAudioBlock.getNumSamples();
    jassert (numSamples == (int) timelineRange.getLength());

    if (timelineRange.intersects (fadeInSampleRange) && fadeInSampleRange.getLength() > 0)
    {
        double alpha1 = 0;
        auto startSamp = int (fadeInSampleRange.getStart() - timelineRange.getStart());

        if (startSamp > 0)
        {
            if (clearExtraSamples)
                destAudioBlock.getSubBlock (0, (size_t) startSamp).clear();
        }
        else
        {
            alpha1 = (timelineRange.getStart() - fadeInSampleRange.getStart()) / (double) fadeInSampleRange.getLength();
            startSamp = 0;
        }

        int endSamp;
        double alpha2;

        if (timelineRange.getEnd() >= fadeInSampleRange.getEnd())
        {
            endSamp = int (timelineRange.getEnd() - fadeInSampleRange.getEnd());
            alpha2 = 1.0;
        }
        else
        {
            endSamp = (int) timelineRange.getLength();
            alpha2 = juce::jmax (0.0, (timelineRange.getEnd() - fadeInSampleRange.getStart()) / (double) fadeInSampleRange.getLength());
        }

        if (endSamp > startSamp)
        {
            auto buffer = tracktion_graph::test_utilities::createAudioBuffer (destAudioBlock);
            AudioFadeCurve::applyCrossfadeSection (buffer,
                                                   startSamp, endSamp - startSamp,
                                                   fadeInType,
                                                   (float) alpha1,
                                                   (float) alpha2);
        }
    }
    
    if (timelineRange.intersects (fadeOutSampleRange) && fadeOutSampleRange.getLength() > 0)
    {
        double alpha1 = 0;
        auto startSamp = int (fadeOutSampleRange.getStart() - timelineRange.getStart());

        if (startSamp <= 0)
        {
            startSamp = 0;
            alpha1 = (timelineRange.getStart() - fadeOutSampleRange.getStart()) / (double) fadeOutSampleRange.getLength();
        }

        int endSamp;
        double alpha2;

        if (timelineRange.getEnd() >= fadeOutSampleRange.getEnd())
        {
            endSamp = int (timelineRange.getEnd() - fadeOutSampleRange.getEnd());
            alpha2 = 1.0;

            if (clearExtraSamples && endSamp < numSamples)
                destAudioBlock.getSubBlock ((size_t) endSamp, size_t (numSamples - endSamp)).clear();
        }
        else
        {
            endSamp = numSamples;
            alpha2 = (timelineRange.getEnd() - fadeOutSampleRange.getStart()) / (double) fadeOutSampleRange.getLength();
        }

        if (endSamp > startSamp)
        {
            auto buffer = tracktion_graph::test_utilities::createAudioBuffer (destAudioBlock);
            AudioFadeCurve::applyCrossfadeSection (buffer,
                                                   startSamp, endSamp - startSamp,
                                                   fadeOutType,
                                                   juce::jlimit (0.0f, 1.0f, (float) (1.0 - alpha1)),
                                                   juce::jlimit (0.0f, 1.0f, (float) (1.0 - alpha2)));
        }
    }
}

bool FadeInOutNode::renderingNeeded (const juce::Range<int64_t>& timelineSampleRange) const
{
    if (! playHeadState.playHead.isPlaying())
        return false;

    return fadeInSampleRange.intersects (timelineSampleRange)
        || fadeOutSampleRange.intersects (timelineSampleRange);
}

} // namespace tracktion_engine
