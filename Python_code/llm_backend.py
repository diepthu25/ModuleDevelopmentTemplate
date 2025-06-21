import torch
from typing import Optional, Dict, List
import json
from pathlib import Path
from dataclasses import dataclass
from transformers import AutoModelForCausalLM, AutoTokenizer

@dataclass
class LLMConfig:
    model_name: str = "microsoft/phi-2"  # Open-access model
    system_prompt: str = "You are VisionAgent, a helpful AI assistant."
    max_history: int = 3
    history_file: str = "conversation_history.json"
    max_new_tokens: int = 100  # Reduced for faster response
    temperature: float = 0.7  # Controls randomness

class LLMBackend:
    def __init__(self, config: LLMConfig = None):
        self.config = config or LLMConfig()
        self.model = None
        self.tokenizer = None
        self.conversation_history = []
        self._load_model()

    def _load_model(self):
        """Load model with proper configuration"""
        try:
            self.tokenizer = AutoTokenizer.from_pretrained(
                self.config.model_name,
                trust_remote_code=True
            )
            
            # Set pad token if not set
            if self.tokenizer.pad_token is None:
                self.tokenizer.pad_token = self.tokenizer.eos_token

            device = "cuda" if torch.cuda.is_available() else "cpu"
            self.model = AutoModelForCausalLM.from_pretrained(
                self.config.model_name,
                torch_dtype="auto",
                trust_remote_code=True
            ).to(device)
            
        except Exception as e:
            raise RuntimeError(f"Model loading failed: {str(e)}")

    def process(self, prompt: str) -> str:
        """Safe text generation with timeout handling"""
        try:
            inputs = self.tokenizer(
                prompt,
                return_tensors="pt",
                truncation=True,
                max_length=512
            ).to(self.model.device)

            # Generate with conservative settings
            with torch.no_grad():  # Reduces memory usage
                outputs = self.model.generate(
                    input_ids=inputs["input_ids"],
                    attention_mask=inputs["attention_mask"],
                    max_new_tokens=self.config.max_new_tokens,
                    temperature=self.config.temperature,
                    do_sample=True,
                    pad_token_id=self.tokenizer.eos_token_id  # Explicitly set
                )

            return self.tokenizer.decode(
                outputs[0][inputs["input_ids"].shape[1]:],
                skip_special_tokens=True
            ).strip()

        except KeyboardInterrupt:
            return "Generation stopped by user."
        except Exception as e:
            print(f"Generation error: {str(e)}")
            return "I encountered a problem. Please try again."

if __name__ == "__main__":
    try:
        print("Initializing VisionAgent...")
        llm = LLMBackend()
        print(f"Model loaded on {llm.model.device}")
        print("Type 'quit' to exit\n")

        while True:
            try:
                user_input = input("You: ")
                if user_input.lower() in ["quit", "exit"]:
                    break
                    
                print("AI:", end=" ", flush=True)  # Start printing response
                response = llm.process(user_input)
                print(response)
                
            except KeyboardInterrupt:
                print("\nType 'quit' to exit or continue chatting")
                continue
                
    except Exception as e:
        print(f"Fatal error: {str(e)}")