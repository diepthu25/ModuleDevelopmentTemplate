import torch
from transformers import AutoTokenizer, AutoModelForCausalLM
from huggingface_hub import login
from typing import List, Dict
import json
from datetime import datetime
import os

class Gemma3Backend:
    """
    Dedicated Gemma 3 LLM backend with:
    - Proper authentication handling
    - Model size options (1B/4B)
    - Conversation history management
    - Error resilience
    """
    
    SUPPORTED_MODELS = {
        '1b': 'google/gemma-3-1b-it',
        '4b': 'google/gemma-3-4b-it'
    }
    
    def __init__(self, model_size: str = '1b', hf_token: str = None):
        """
        Initialize Gemma 3 backend
        
        Args:
            model_size: '1b' or '4b'
            hf_token: HuggingFace token (or set HF_TOKEN environment variable)
        """
        self.model_size = model_size.lower()
        assert self.model_size in self.SUPPORTED_MODELS, \
               f"Unsupported model size. Choose from: {list(self.SUPPORTED_MODELS.keys())}"
        
        self.device = 'cuda' if torch.cuda.is_available() else 'cpu'
        self.history = []
        
        # Handle authentication
        self._authenticate(hf_token)
        
        # Initialize model
        self.model_name = self.SUPPORTED_MODELS[self.model_size]
        self.tokenizer = None
        self.model = None
        self._load_model()
        
        # System prompt
        self.system_prompt = {
            "role": "system",
            "content": "You are VisionAgent, a helpful AI assistant.",
            "timestamp": str(datetime.now())
        }
        self.history.append(self.system_prompt)
    
    def _authenticate(self, token: str = None):
        """Handle HuggingFace authentication"""
        token = token or os.getenv('HF_TOKEN')
        if not token:
            raise ValueError(
                "HuggingFace token required for Gemma 3.\n"
                "Get one at: https://huggingface.co/settings/tokens\n"
                "Then either:\n"
                "1. Pass as hf_token parameter\n"
                "2. Set HF_TOKEN environment variable"
            )
        login(token=token, add_to_git_credential=True)
    
    def _load_model(self):
        """Load the model and tokenizer"""
        try:
            self.tokenizer = AutoTokenizer.from_pretrained(self.model_name)
            self.model = AutoModelForCausalLM.from_pretrained(
                self.model_name,
                device_map="auto",
                torch_dtype=torch.float16 if 'cuda' in self.device else torch.float32
            )
            print(f"✅ Loaded Gemma 3 {self.model_size} on {self.device}")
        except Exception as e:
            raise RuntimeError(f"Failed to load model: {str(e)}")
    
    def chat(self, prompt: str) -> str:
        """
        Process user input and generate response
        
        Args:
            prompt: User's text input
            
        Returns:
            Generated response
        """
        try:
            # Add to history
            self._add_message("user", prompt)
            
            # Format prompt with chat template
            messages = [{"role": msg["role"], "content": msg["content"]} 
                       for msg in self.history]
            formatted_prompt = self.tokenizer.apply_chat_template(
                messages,
                tokenize=False,
                add_generation_prompt=True
            )
            
            # Tokenize
            inputs = self.tokenizer(
                formatted_prompt,
                return_tensors="pt",
                truncation=True
            ).to(self.device)
            
            # Generate
            outputs = self.model.generate(
                **inputs,
                max_new_tokens=512,
                temperature=0.7,
                do_sample=True
            )
            
            # Decode and clean
            response = self.tokenizer.decode(
                outputs[0][inputs.input_ids.shape[-1]:],
                skip_special_tokens=True
            )
            
            # Add to history and return
            self._add_message("assistant", response)
            return response
            
        except Exception as e:
            self._add_message("system", f"Error: {str(e)}")
            return f"⚠️ Error processing request: {str(e)}"
    
    def _add_message(self, role: str, content: str):
        """Add message to history with context management"""
        self.history.append({
            "role": role,
            "content": content,
            "timestamp": str(datetime.now())
        })
        
        # Keep last 5 exchanges to manage context
        if len(self.history) > 11:  # system + 5 user + 5 assistant
            self.history = [self.history[0]] + self.history[-10:]
    
    def clear_history(self):
        """Reset conversation while keeping system prompt"""
        self.history = [self.system_prompt]
    
    def save_chat(self, file_path: str):
        """Save conversation to JSON file"""
        with open(file_path, 'w') as f:
            json.dump(self.history, f, indent=2)
    
    def load_chat(self, file_path: str):
        """Load conversation from JSON file"""
        with open(file_path, 'r') as f:
            self.history = json.load(f)

# Test the implementation
if __name__ == "__main__":
    # Initialize with your HuggingFace token
    hf_token = "your_hf_token_here"  # Replace with actual token
    
    print("🚀 Testing Gemma 3 Backend")
    llm = Gemma3Backend(model_size='1b', hf_token=hf_token)
    
    # Test conversation
    queries = [
        "Explain quantum computing basics",
        "How does a transformer model work?",
        "What makes Gemma 3 special?"
    ]
    
    for query in queries:
        print(f"\nYou: {query}")
        response = llm.chat(query)
        print(f"AI: {response}")
    
    # Show history
    print("\nConversation History:")
    print(json.dumps(llm.history, indent=2))