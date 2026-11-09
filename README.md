# GuitarPedalDev
A software engine meant to run my own multi-effects unit

Right now, this project does nothing like this. What you will actually see is an application that integrates Port Audio and uses it to capture audio signal from my Motu M2 Audio Interface. This is an interim step, to allow me to develop effects on a Windows platform. As these effects become ready to deploy to a microcontroller, I’ll swap out PortAudio for a Daisy Seed3’s own audio library functions.

# How to get started:
To use this yourself, you would to take two steps:
1. You would need to build Port Audio as a shared library for your OS. If you are on Windows, the libportaudio.dll already in the repo might suffice for this
2. You would need to edit pedal_processor.c to select your own audio interface. This happens at the top of main, and you will find it is currently hard-coded for my own Motu M2. In src/util you will find a source file "list_audio_device.c" If you compile and run this, you should see a list of ever audio device detected by Port Audio, and you should find the info you need to set your Audio interface in that list.

You can find a makefile that should allow you to build the application, but you may need to edit that to make sure the PA shared library is detected.


# Application structure in brief:

pedal_processor.c is where it all happens. It has a main and a callback function. Main initializes everything needed for Audio processing, finds my audio interface and gets its details from the OS, configures the audio stream, and then starts it.

callback runs repeatedly as audio arrives. It hands off pointers to input and output buffers to a series of processing objects. Effects are initialized in a set signal chain, and the signal path is the order in which these objects are appended to the chain. 

Each "engine" processes output in blocks, defined by "Frame_per_buffer" in pedal_processor.h

distortion_effects handles the creation and initialization of a distortion engine object. This object takes a complete audio buffer and handles any processing that needs to happen on the entire buffer(like oversampling). The engine handles any buffer level processing, and then loops through a set of distortion_stages.

EQ_effects implements a 3 band equalization effect, with sweep controls for the Bass, Mid, and Treble bands

Compressor_effects implements a compressor with five parameters: Attack, Release, Makeup gain, threshold, and ratio.

Delay_effects implements a 3 parameter delay(Dry/Wet, Delay time, Echo)

Modulation_effects implements Chorus, Flanger, and Phaser
	- Chorus parameters: Dry/Wet mix, Rate, Depth
	- Flanger parameters: Rate, Depth, feedback, feedforward, blend, tone(hpf)
	- Phaser parameters: Rate, Depth, Dry level, Wet level

Reverb_effects implements two reverb designs.
	- 5 allpass filters in series
	- A Freeverb implementation (8 comb filters in parallel, into 4 allpass in series)

Master_effects handles Master control of the application, and for now handles only Master Volume, which is processed at the very end of the signal chain.

The signal chain is defined at runtime in main, and callback routes audio through the each module by looping though the fx_chain


The application has a very simple UI written in Python. This UI broadcasts control signals with UDP, and the C application has a control layer(ui_listener.c) that parses these control signals and updates some shared memory. This has since been expanded to read control definitions from each effects module, and generate the desired controls on a virtual control panel.

On the subject of threading:
The main function of pedal_processor starts the Audio stream, which then runs on a separate thread(callback) while Main blocks until the stream is closed. 
ui_listener runs on its own thread, and this frees it from any real-time constraints. The control layer uses atomic operations to make changes to shared parameter variables that callback can read atomically.
