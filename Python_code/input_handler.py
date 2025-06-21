import vosk
import pyaudio
from typing import Optional

class InputHandler:
    """
    Handles audio input using Vosk speech recognition library.
    Supports Vietnamese language with the vosk-model-small-vn-0.3 model.
    
    Attributes:
        model_path (str): Path to the Vosk language model
        sample_rate (int): Audio sample rate (default 16000)
        model (vosk.Model): Loaded Vosk model
        recognizer (vosk.KaldiRecognizer): Speech recognizer
        audio_stream (pyaudio.Stream): Audio input stream
    """
    
    def __init__(self, model_path: str = "vosk-model-small-vn-0.3", sample_rate: int = 16000):
        """
        Initialize the input handler with specified model.
        
        Args:
            model_path: Path to Vosk model directory
            sample_rate: Audio sample rate in Hz
        """
        self.model_path = model_path
        self.sample_rate = sample_rate
        self.model = vosk.Model(model_path)
        self.recognizer = vosk.KaldiRecognizer(self.model, sample_rate)
        self.audio_stream = None
        
    def start_stream(self):
        """Initialize and start the audio input stream."""
        audio_interface = pyaudio.PyAudio()
        self.audio_stream = audio_interface.open(
            format=pyaudio.paInt16,
            channels=1,
            rate=self.sample_rate,
            input=True,
            frames_per_buffer=4096
        )
        
    def stop_stream(self):
        """Stop and close the audio input stream."""
        if self.audio_stream:
            self.audio_stream.stop_stream()
            self.audio_stream.close()
            
    def listen(self) -> Optional[str]:
        """
        Listen for audio input and return transcribed text.
        
        Returns:
            str: Transcribed text or None if no speech detected
        """
        if not self.audio_stream:
            self.start_stream()
            
        while True:
            data = self.audio_stream.read(4096, exception_on_overflow=False)
            if self.recognizer.AcceptWaveform(data):
                result = self.recognizer.Result()
                text = eval(result).get("text", "")
                if text:
                    return text
            # Add a small delay to prevent CPU overuse
            time.sleep(0.1)

if __name__ == "__main__":
    # Test the input handler
    print("Testing InputHandler...")
    handler = InputHandler()
    try:
        print("Speak now...")
        text = handler.listen()
        print(f"You said: {text}")
    except KeyboardInterrupt:
        print("Stopping...")
    finally:
        handler.stop_stream()