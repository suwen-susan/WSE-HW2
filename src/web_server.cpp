#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <iomanip> 
#include <algorithm>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include "index_reader.hpp"
#include "bm25.hpp"
#include "utils.hpp"
#include "querier.hpp"


// HTTP Server
class WebServer {
private:
    int port;
    SOCKET serverSocket;
    
    // Index components
    Lexicon* lexicon;
    Stats* stats;
    DocLen* docLen;
    DocTable* docTable;
    DocContentFile* docContent; 
    std::string indexDir;
    bm25::Params bm25Params;

    QueryEvaluator* evaluator;
    std::mutex evalMutex;

    // URL Decode
    std::string urlDecode(const std::string& str) {
        std::string result;
        for (size_t i = 0; i < str.length(); i++) {
            if (str[i] == '%' && i + 2 < str.length()) {
                int value;
                std::istringstream is(str.substr(i + 1, 2));
                if (is >> std::hex >> value) {
                    result += static_cast<char>(value);
                    i += 2;
                } else {
                    result += str[i];
                }
            } else if (str[i] == '+') {
                result += ' ';
            } else {
                result += str[i];
            }
        }
        return result;
    }
    
    // extract URL parameter
    std::string getParam(const std::string& queryString, const std::string& key) {
        size_t pos = queryString.find(key + "=");
        if (pos == std::string::npos) return "";
        
        pos += key.length() + 1;
        size_t endPos = queryString.find('&', pos);
        if (endPos == std::string::npos) {
            return urlDecode(queryString.substr(pos));
        }
        return urlDecode(queryString.substr(pos, endPos - pos));
    }

    // Escape JSON special characters
    std::string escapeJson(const std::string& str) {
        std::ostringstream escaped;
        for (char c : str) {
            switch (c) {
                case '"': escaped << "\\\""; break;
                case '\\': escaped << "\\\\"; break;
                case '\b': escaped << "\\b"; break;
                case '\f': escaped << "\\f"; break;
                case '\n': escaped << "\\n"; break;
                case '\r': escaped << "\\r"; break;
                case '\t': escaped << "\\t"; break;
                default:
                    if (c < 32) {
                        escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)(unsigned char)c;
                    } else {
                        escaped << c;
                    }
            }
        }
        return escaped.str();
    }

    // generate JSON response
    std::string generateJsonResponse(const std::vector<QueryResult>& results, 
                                    const std::vector<std::string>& queryTerms,
                                    long long queryTime) {
        std::ostringstream json;
        json << "{\n";
        json << "  \"query_terms\": [";
        for (size_t i = 0; i < queryTerms.size(); i++) {
            if (i > 0) json << ", ";
            json << "\"" << escapeJson(queryTerms[i]) << "\"";
        }
        json << "],\n";
        json << "  \"query_time_ms\": " << queryTime << ",\n";
        json << "  \"num_results\": " << results.size() << ",\n";
        json << "  \"results\": [\n";
        

        // get document contents in batch
        std::vector<uint32_t> docIDs;
        for (const auto& r : results) {
            docIDs.push_back(r.docID);
        }
        auto contents = docContent->getBatch(docIDs);
        
        for (size_t i = 0; i < results.size(); i++) {
            if (i > 0) json << ",\n";
            
            uint32_t docID = results[i].docID;
            
            // generate query-dependent snippet
            std::string snippet;
            auto it = contents.find(docID);
            if (it != contents.end() && !it->second.empty()) {
                snippet = SnippetGenerator::generate(it->second, queryTerms);
            } else {
                snippet = "(No content available)";
            }
            
            json << "    {\n";
            json << "      \"rank\": " << (i + 1) << ",\n";
            json << "      \"docID\": " << docID << ",\n";
            json << "      \"score\": " << std::fixed << std::setprecision(4) << results[i].score << ",\n";
            json << "      \"original_id\": \"" << escapeJson(docTable->originalID(docID)) << "\",\n";
            json << "      \"snippet\": \"" << escapeJson(snippet) << "\"\n";
            json << "    }";
        }
        
        json << "\n  ]\n";
        json << "}";
        
        return json.str();
    }


    // Read static file
    std::string readFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return "";
        
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
    
    // Get content type based on file extension
    std::string getContentType(const std::string& path) {
        if (path.find(".html") != std::string::npos) return "text/html";
        if (path.find(".css") != std::string::npos) return "text/css";
        if (path.find(".js") != std::string::npos) return "application/javascript";
        if (path.find(".json") != std::string::npos) return "application/json";
        return "text/plain";
    }
    
    // send HTTP response
    void sendResponse(SOCKET clientSocket, const std::string& status, 
                     const std::string& contentType, const std::string& body) {
        std::ostringstream response;
        response << "HTTP/1.1 " << status << "\r\n";
        response << "Content-Type: " << contentType << "; charset=utf-8\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << body;
        
        std::string resp = response.str();
        send(clientSocket, resp.c_str(), resp.length(), 0);
    }
    
    // handle client connection
    void handleClient(SOCKET clientSocket) {
        char buffer[4096];
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesRead <= 0) {
            closesocket(clientSocket);
            return;
        }
        
        buffer[bytesRead] = '\0';
        std::string request(buffer);
        
        // parse request line
        size_t methodEnd = request.find(' ');
        size_t pathEnd = request.find(' ', methodEnd + 1);
        
        if (methodEnd == std::string::npos || pathEnd == std::string::npos) {
            sendResponse(clientSocket, "400 Bad Request", "text/plain", "Bad Request");
            closesocket(clientSocket);
            return;
        }
        
        std::string method = request.substr(0, methodEnd);
        std::string fullPath = request.substr(methodEnd + 1, pathEnd - methodEnd - 1);
        
        // split path and query string
        std::string path = fullPath;
        std::string queryString;
        size_t queryPos = fullPath.find('?');
        if (queryPos != std::string::npos) {
            path = fullPath.substr(0, queryPos);
            queryString = fullPath.substr(queryPos + 1);
        }
        
        std::cout << "Request: " << method << " " << path << std::endl;
        
        // route requests
        if (path == "/" || path == "/index.html") {
            std::string html = readFile("web/index.html");
            if (html.empty()) {
                sendResponse(clientSocket, "404 Not Found", "text/plain", "File not found");
            } else {
                sendResponse(clientSocket, "200 OK", "text/html", html);
            }
        } else if (path == "/styles.css") {
            std::string css = readFile("web/styles.css");
            if (css.empty()) {
                sendResponse(clientSocket, "404 Not Found", "text/plain", "File not found");
            } else {
                sendResponse(clientSocket, "200 OK", "text/css", css);
            }
        } else if (path == "/search") {
            std::string query = getParam(queryString, "q");
            std::string modeStr = getParam(queryString, "mode");
            std::string kStr = getParam(queryString, "k");
            std::string k1Str = getParam(queryString, "k1");
            std::string bStr = getParam(queryString, "b");
            
            std::string mode = modeStr;
            int k = kStr.empty() ? 10 : std::stoi(kStr);
            double k1 = k1Str.empty() ? 0.9 : std::stod(k1Str);
            double b = bStr.empty() ? 0.4 : std::stod(bStr);
            auto startTime = std::chrono::high_resolution_clock::now();
            
            // tokenize query
            std::vector<std::string> tokens = tokenize_words(query);
            std::unordered_set<std::string> uniqueSet(tokens.begin(), tokens.end());
            std::vector<std::string> queryTerms(uniqueSet.begin(), uniqueSet.end());

            // execute query
            std::vector<QueryResult> results;
            {
                std::lock_guard<std::mutex> lock(evalMutex);
                evaluator->updateBM25Params(k1, b);
                results = evaluator->processQuery(queryTerms, mode, k);
            }
           
            auto endTime = std::chrono::high_resolution_clock::now();
            long long queryTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime).count();
            
            // generate JSON response
            std::string json = generateJsonResponse(results, queryTerms, queryTime);
            sendResponse(clientSocket, "200 OK", "application/json", json);
            
        } else {
            sendResponse(clientSocket, "404 Not Found", "text/plain", "Not Found");
        }
        
        closesocket(clientSocket);
    }
    
public:
    WebServer(int p, Lexicon* lex, Stats* st, DocLen* dl, DocTable* dt, DocContentFile* dc,
              const std::string& idxDir, bm25::Params params)
        : port(p), serverSocket(INVALID_SOCKET), 
          lexicon(lex), stats(st), docLen(dl), docTable(dt), docContent(dc),
          indexDir(idxDir), bm25Params(params), evaluator(nullptr) {
        
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
evaluator = new QueryEvaluator(*lexicon, *stats, *docLen, *docTable, *docContent, indexDir, params);
    }
    
    ~WebServer() {
        if (evaluator) {
            delete evaluator;
        }
        
        if (serverSocket != INVALID_SOCKET) {
            closesocket(serverSocket);
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    bool start() {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        // allow address reuse
        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, 
                  (char*)&opt, sizeof(opt));
        
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port);
        
        if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Failed to bind socket" << std::endl;
            return false;
        }
        
        if (listen(serverSocket, 10) == SOCKET_ERROR) {
            std::cerr << "Failed to listen" << std::endl;
            return false;
        }
        
        std::cout << "Web server started at http://localhost:" << port << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        
        // connection loop
        while (true) {
            sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            
            SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);
            if (clientSocket == INVALID_SOCKET) {
                continue;
            }
            
            // handle client in a new thread
            std::thread clientThread(&WebServer::handleClient, this, clientSocket);
            clientThread.detach();
        }
        
        return true;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <index_dir> <doc_table_path> [port]" << std::endl;
        std::cout << "Example: " << argv[0] << " ./index ./output/doc_table.txt 8080" << std::endl;
        return 1;
    }
    
    std::string indexDir = argv[1];
    std::string docTablePath = argv[2];
    int port = (argc >= 4) ? std::stoi(argv[3]) : 8080;
    
    std::cout << "Loading index..." << std::endl;
    
    // ---- Load index components ----
    Lexicon lexicon;
    if (!lexicon.load(indexDir + "/lexicon.tsv")) {
        return 1;
    }
    
    Stats stats;
    if (!stats.load(indexDir + "/stats.txt")) {
        return 1;
    }
    
    DocLen docLen;
    if (!docLen.load(indexDir + "/doc_len.bin")) {
        return 1;
    }
    
    DocTable docTable;
    if (!docTable.load(docTablePath)) {
        return 1;
    }
    
    // ---- Load document content ----
    DocContentFile docContent;
    std::string offsetPath = docTablePath;
    std::string contentPath = docTablePath;
    size_t lastSlash = offsetPath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        offsetPath = offsetPath.substr(0, lastSlash + 1) + "doc_offset.bin";
        contentPath = contentPath.substr(0, lastSlash + 1) + "doc_content.bin";
    } else {
        offsetPath = "doc_offset.bin";
        contentPath = "doc_content.bin";
    }
    
    if (!docContent.load(offsetPath, contentPath)) {
        std::cerr << "Warning: Could not load document content, snippets will be unavailable" << std::endl;
    }

    std::cout << "Index loaded successfully!" << std::endl;
    
    // start web server
    bm25::Params bm25Params(0.9, 0.4);
    WebServer server(port, &lexicon, &stats, &docLen, &docTable, &docContent, indexDir, bm25Params);
    
    if (!server.start()) {
        return 1;
    }
    
    return 0;
}