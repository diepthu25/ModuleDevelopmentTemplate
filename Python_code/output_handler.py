import pyttsx3
from typing import Optional

class OutputHandler:
    """
    Handles text-to-speech output using pyttsx3 library.
    Configured for Vietnamese language support.
    
    Attributes:
        engine (pyttsx3.Engine): TTS engine
        rate (int): Speech rate (words per minute)
        volume (float): Speech volume (0.0 to 1.0)
    """
    
    def __init__(self, rate: int = 150, volume: float = 0.9):
        """
        Initialize the output handler.
        
        Args:
            rate: Speech rate in words per minute
            volume: Speech volume (0.0 to 1.0)
        """
        self.engine = pyttsx3.init()
        self.rate = rate
        self.volume = volume
        
        # Configure Vietnamese voice if available
        voices = self.engine.getProperty('voices')
        for voice in voices:
            if 'vietnamese' in voice.languages or 'vi' in voice.languages:
                self.engine.setProperty('voice', voice.id)
                break
                
        self.engine.setProperty('rate', self.rate)
        self.engine.setProperty('volume', self.volume)
        
    def speak(self, text: str):
        """
        Convert text to speech and play it.
        
        Args:
            text: Text to be spoken
        """
        self.engine.say(text)
        self.engine.runAndWait()
        
    def set_rate(self, rate: int):
        """Set speech rate."""
        self.rate = rate
        self.engine.setProperty('rate', self.rate)
        
    def set_volume(self, volume: float):
        """Set speech volume."""
        self.volume = volume
        self.engine.setProperty('volume', self.volume)

if __name__ == "__main__":
    # Test the output handler
    print("Testing OutputHandler...")
    handler = OutputHandler()
    test_text = "Xin chào, tôi là VisionAgent, trợ lý ảo của bạn."
    print(f"Speaking: {test_text}")
    handler.speak(test_text)