#pragma once

#include <string>
#include <map>
#include <vector>
#include <utility>
#include <memory>
#include <thread>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <condition_variable>
#include "modules/ModuleLLM/ThreadSafeQueue.h"
#include "ILLMInferenceBackend.h"
#include "Logger.h"

// Structs
struct LLMResourceConfig {
    std::string model_path;
    std::string tokenizer_path;
    std::string system_prompt;
    int max_context_length;
    int max_new_tokens;
    float temperature = 0.7f;
    float top_p = 0.9f;

    enum class BackendType { TENSORFLOW, ARMNN };
    BackendType selected_backend;

    std::string history_persistence_path = "./sys/data/chat/";
};

struct LLMProcessingTask {
    std::string request_id;
    std::string session_id;
    std::string user_query;
    std::vector<std::pair<std::string, std::string>> current_history;
    std::string system_prompt_snapshot;
};

struct LLMOutputData {
    std::string request_id;
    std::string session_id;
    std::string response_text;
    bool success = false;
    std::string error_message;
};

class LLMProcessorModule {
public:
    LLMProcessorModule();
    ~LLMProcessorModule();

    bool Setup();
    void Loop();  // Minimal: workers do heavy lifting
    void Exit();

    std::string StartNewConversation();
    bool SwitchConversation(const std::string& session_id);
    std::string SubmitQuery(const std::string& session_id, const std::string& user_query);
    std::vector<std::pair<std::string, std::string>> GetConversationHistory(const std::string& session_id);

private:
    std::string m_active_session_id;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> m_session_histories;

    ThreadSafeQueue<LLMProcessingTask> m_request_queue;
    std::vector<std::thread> m_worker_threads;
    std::atomic<uint64_t> m_request_id_counter = 0;

    std::unique_ptr<ILLMInferenceBackend> m_llm_backend;
    LLMResourceConfig m_config;

    std::string GenerateSessionID();
    std::string GenerateRequestID();
    void WorkerLoop();

    void SaveConversation(const std::string& session_id);
    void LoadConversation(const std::string& session_id);
    void PublishResponse(const LLMOutputData& output);
};
