#include "modules/ModuleLLM/LLM.h"
//#include "FileIO.h"        // load/save JSON history
//#include "BackendFactory.h" // factory to create backend based on config

LLMProcessorModule::LLMProcessorModule() {}

LLMProcessorModule::~LLMProcessorModule() {
    Exit();
}

bool LLMProcessorModule::Setup() {
    Logger::Info("LLMProcessor: Setup started.");

    // Load resource config 
    if (!LoadLLMResourceConfig(m_config)) {
        Logger::Error("LLMProcessor: Failed to load config.");
        return false;
    }

    m_llm_backend = BackendFactory::CreateBackend(m_config.selected_backend);
    if (!m_llm_backend->Initialize(m_config)) {
        Logger::Error("LLMProcessor: Backend failed to initialize.");
        return false;
    }

    std::filesystem::create_directories(m_config.history_persistence_path);

    for (int i = 0; i < std::thread::hardware_concurrency(); ++i)
        m_worker_threads.emplace_back(&LLMProcessorModule::WorkerLoop, this);

    Logger::Info("LLMProcessor: Setup complete.");
    return true;
}

void LLMProcessorModule::Loop() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void LLMProcessorModule::Exit() {
    m_request_queue.Shutdown();
    for (auto& t : m_worker_threads)
        if (t.joinable()) t.join();

    for (auto& [session_id, _] : m_session_histories)
        SaveConversation(session_id);

    Logger::Info("LLMProcessor: Clean shutdown.");
}

std::string LLMProcessorModule::StartNewConversation() {
    std::string session_id = GenerateSessionID();
    m_session_histories[session_id] = {};
    SaveConversation(session_id);
    return session_id;
}

bool LLMProcessorModule::SwitchConversation(const std::string& session_id) {
    if (m_session_histories.find(session_id) == m_session_histories.end())
        LoadConversation(session_id);

    if (m_session_histories.count(session_id)) {
        m_active_session_id = session_id;
        return true;
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> LLMProcessorModule::GetConversationHistory(const std::string& session_id) {
    if (m_session_histories.find(session_id) == m_session_histories.end())
        LoadConversation(session_id);
    return m_session_histories[session_id];
}

std::string LLMProcessorModule::SubmitQuery(const std::string& session_id, const std::string& user_query) {
    if (m_session_histories.find(session_id) == m_session_histories.end())
        LoadConversation(session_id);
    if (!m_session_histories.count(session_id)) return "";

    std::string request_id = GenerateRequestID();

    LLMProcessingTask task {
        .request_id = request_id,
        .session_id = session_id,
        .user_query = user_query,
        .current_history = m_session_histories[session_id],
        .system_prompt_snapshot = m_config.system_prompt
    };

    m_request_queue.Enqueue(task);
    return request_id;
}

void LLMProcessorModule::WorkerLoop() {
    LLMProcessingTask task;
    while (m_request_queue.Dequeue(task)) {
        std::string response;
        LLMOutputData output{
            .request_id = task.request_id,
            .session_id = task.session_id
        };

        bool ok = m_llm_backend->Process(task, response);
        output.success = ok;
        output.response_text = response;
        output.error_message = ok ? "" : "LLM failed to process query.";

        if (ok) {
            m_session_histories[task.session_id].emplace_back("user", task.user_query);
            m_session_histories[task.session_id].emplace_back("assistant", response);
            SaveConversation(task.session_id);
        }

        PublishResponse(output);
    }
}

void LLMProcessorModule::PublishResponse(const LLMOutputData& output) {
    std::string key = "thevision.llm.response." + output.request_id;
    Logger::Info("Publishing response to: " + key);
    // Replace with VisionRT publish method
}

void LLMProcessorModule::SaveConversation(const std::string& session_id) {
    const auto& history = m_session_histories[session_id];
    FileIO::SaveHistoryToFile(m_config.history_persistence_path + session_id + ".json", history);
}

void LLMProcessorModule::LoadConversation(const std::string& session_id) {
    auto history = FileIO::LoadHistoryFromFile(m_config.history_persistence_path + session_id + ".json");
    if (!history.empty())
        m_session_histories[session_id] = history;
}

std::string LLMProcessorModule::GenerateSessionID() {
    return UUIDGenerator::Generate();  // or use std::random_device-based implementation
}

std::string LLMProcessorModule::GenerateRequestID() {
    return "req_" + std::to_string(++m_request_id_counter);
}
