# TestWindowsRuntimeAPI

A test app that uses the Windows Runtime API to play back an adaptive media stream and route its audio to an AudioGraph.

This app's code is based on https://github.com/wanjawischmeier/MediaPlayback but has been simplified in order to not need Unity.

In this app, we would like to use a single IMediaSource2 component to feed both the media player (for grabbing the video frames in AdaptiveStreamer::OnVideoFrameAvailable) and the Audio Graph (to read the audio buffers).
We want to use the Audio Graph to grab the audio buffers coming in from the adaptive media stream, given that this operation is impossible in the IMediaSource APIs.

To validate the bug, please follow these steps:
- open the VS project
- build in Debug x64
- put a breakpoint in AdaptiveStreamer::OnFailed
- run and debug

You will notice that AdaptiveStreamer::OnFailed is called with an error message stating "Some component is already listening to events on this event generator". Also, AdaptiveStreamer::OnVideoFrameAvailable will never get called.

If you comment out #define ONE_SINGLE_MEDIASOURCE, the code will work and AdaptiveStreamer::OnVideoFrameAvailable will be called. No error will be reported in AdaptiveStreamer::OnFailed and playback will work. However, we expect synchronization issues with that approach and would like to use the same media source for both the video frames and the audio buffers.