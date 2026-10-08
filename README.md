# GuitarPedalDev
A software engine meant to run my own multi-effects unit

Right now, this project does nothing like this. What you will actually see is an application that integrates Port Audio and uses it to capture audio signal from my Motu M2 Audio Interface. This is an interim step, to allow me to develop effects on a Windows platform. As these effects become ready to deploy to a microcontroller, I’ll swap out PortAudio for a Daisy Seed3’s own audio library functions.

Application structure in brief:

pedal_processor.c is where it all happens. It has a main and a callback function. Main initializes everything needed for Audio processing, finds my audio interface and gets its details from the OS, configures the audio stream, and then starts it.


callback runs repeatedly as audio arrives. It hands off pointers to input and output buffers to a series of processing objects. Right now just two, a distortion effect and an EQ. Long term it should be possible


distortion_effects handles the creation and initialization of a distortion engine object. This object takes a complete audio buffer and handles any processing that needs to happen on the entire buffer(like oversampling). The engine handles any buffer level processing, and then loops through a set of distortion_stages that process the signal sample-by-sample

EQ_effects implements a 3 band equalization effect, with sweep controls for the Bass, Mid, and Treble bands

The application has a very simple UI written in Python. This UI broadcasts control signals with UDP, and the C application has a control layet(ui_listener.c) that parses these control signals and updates some shared memory. 

On the subject of threading:
The main function of pedal_processor starts the Audio stream, which then runs on a separate thread(callback) while Main blocks until the stream is closed. 
ui_listener runs on its own thread, and this frees it from any real-time constraints. The control layer uses atomic operations to make changes to shared parameter variables that callback can read atomically.
