#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Url {
    std::string host;
    std::string port = "80";
    std::string path = "/";
};

struct HttpResponse {
    int statusCode = 0;
    std::map<std::string, std::string> headers;
    std::string bodyPrefix;
};

void printUsage(const char* program) {
    std::cerr
        << "Usage:\n"
        << "  " << program << " <http-url> [output-file]\n\n"
        << "Example:\n"
        << "  " << program << " http://example.com/video.mp4 video.mp4\n";
}

std::string toLower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string trim(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(begin, end - begin);
}

bool parseUrl(const std::string& input, Url& url) {
    const std::string prefix = "http://";
    if (input.rfind(prefix, 0) != 0) {
        std::cerr << "Only plain HTTP URLs are supported in this learning demo." << std::endl;
        return false;
    }

    std::string rest = input.substr(prefix.size());
    if (rest.empty()) {
        return false;
    }

    size_t pathPos = rest.find('/');
    std::string authority = pathPos == std::string::npos ? rest : rest.substr(0, pathPos);
    url.path = pathPos == std::string::npos ? "/" : rest.substr(pathPos);

    size_t portPos = authority.rfind(':');
    if (portPos != std::string::npos) {
        url.host = authority.substr(0, portPos);
        url.port = authority.substr(portPos + 1);
    } else {
        url.host = authority;
        url.port = "80";
    }

    return !url.host.empty() && !url.port.empty();
}

std::string defaultOutputName(const Url& url) {
    size_t slash = url.path.find_last_of('/');
    std::string name = slash == std::string::npos ? url.path : url.path.substr(slash + 1);
    if (name.empty()) {
        return "download.bin";
    }

    size_t query = name.find('?');
    if (query != std::string::npos) {
        name = name.substr(0, query);
    }

    return name.empty() ? "download.bin" : name;
}

long long existingFileSize(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return 0;
    }
    return static_cast<long long>(input.tellg());
}

int connectTcp(const Url& url) {
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    int rc = getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &results);
    if (rc != 0) {
        std::cerr << "getaddrinfo failed: " << gai_strerror(rc) << std::endl;
        return -1;
    }

    int fd = -1;
    for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
        fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (fd == -1) {
            continue;
        }

        if (connect(fd, item->ai_addr, item->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(results);
    return fd;
}

bool sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("send");
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool readHeaders(int fd, HttpResponse& response) {
    std::string buffer;
    std::vector<char> chunk(4096);

    while (true) {
        ssize_t n = recv(fd, chunk.data(), chunk.size(), 0);
        if (n <= 0) {
            std::cerr << "Connection closed before HTTP headers were complete." << std::endl;
            return false;
        }

        buffer.append(chunk.data(), static_cast<size_t>(n));
        size_t headerEnd = buffer.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            continue;
        }

        std::string headerText = buffer.substr(0, headerEnd);
        response.bodyPrefix = buffer.substr(headerEnd + 4);

        std::istringstream stream(headerText);
        std::string statusLine;
        if (!std::getline(stream, statusLine)) {
            return false;
        }

        std::istringstream status(statusLine);
        std::string httpVersion;
        status >> httpVersion >> response.statusCode;

        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            size_t colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }

            response.headers[toLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
        }

        return response.statusCode > 0;
    }
}

long long parseContentLength(const HttpResponse& response) {
    auto it = response.headers.find("content-length");
    if (it == response.headers.end()) {
        return -1;
    }

    try {
        return std::stoll(it->second);
    } catch (...) {
        return -1;
    }
}

void printProgress(long long downloaded, long long total) {
    if (total > 0) {
        double percent = static_cast<double>(downloaded) * 100.0 / static_cast<double>(total);
        std::cout << "\rDownloaded " << downloaded << " / " << total
                  << " bytes (" << static_cast<int>(percent) << "%)" << std::flush;
    } else {
        std::cout << "\rDownloaded " << downloaded << " bytes" << std::flush;
    }
}

int download(const std::string& urlText, const std::string& outputPath) {
    Url url;
    if (!parseUrl(urlText, url)) {
        return 1;
    }

    long long resumeFrom = existingFileSize(outputPath);
    int fd = connectTcp(url);
    if (fd == -1) {
        std::cerr << "Failed to connect to " << url.host << ":" << url.port << std::endl;
        return 1;
    }

    std::ostringstream request;
    request << "GET " << url.path << " HTTP/1.1\r\n"
            << "Host: " << url.host << "\r\n"
            << "User-Agent: av-study-http-downloader/1.0\r\n"
            << "Accept: */*\r\n"
            << "Connection: close\r\n";

    if (resumeFrom > 0) {
        request << "Range: bytes=" << resumeFrom << "-\r\n";
    }
    request << "\r\n";

    if (!sendAll(fd, request.str())) {
        close(fd);
        return 1;
    }

    HttpResponse response;
    if (!readHeaders(fd, response)) {
        close(fd);
        return 1;
    }

    if (response.headers.count("transfer-encoding") &&
        toLower(response.headers["transfer-encoding"]).find("chunked") != std::string::npos) {
        std::cerr << "\nChunked transfer is not supported in this basic demo." << std::endl;
        close(fd);
        return 1;
    }

    bool append = false;
    if (response.statusCode == 206 && resumeFrom > 0) {
        append = true;
        std::cout << "Resume from byte " << resumeFrom << std::endl;
    } else if (response.statusCode == 200) {
        resumeFrom = 0;
    } else {
        std::cerr << "Unexpected HTTP status: " << response.statusCode << std::endl;
        close(fd);
        return 1;
    }

    std::ofstream output(outputPath,
                         std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!output) {
        std::cerr << "Failed to open output file: " << outputPath << std::endl;
        close(fd);
        return 1;
    }

    long long contentLength = parseContentLength(response);
    long long total = contentLength >= 0 ? resumeFrom + contentLength : -1;
    long long downloaded = resumeFrom;

    if (!response.bodyPrefix.empty()) {
        output.write(response.bodyPrefix.data(), static_cast<std::streamsize>(response.bodyPrefix.size()));
        downloaded += static_cast<long long>(response.bodyPrefix.size());
        printProgress(downloaded, total);
    }

    std::vector<char> buffer(8192);
    while (true) {
        ssize_t n = recv(fd, buffer.data(), buffer.size(), 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            close(fd);
            return 1;
        }

        output.write(buffer.data(), n);
        downloaded += n;
        printProgress(downloaded, total);
    }

    std::cout << "\nSaved to " << outputPath << std::endl;
    close(fd);
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        printUsage(argv[0]);
        return 1;
    }

    Url url;
    if (!parseUrl(argv[1], url)) {
        printUsage(argv[0]);
        return 1;
    }

    std::string outputPath = argc == 3 ? argv[2] : defaultOutputName(url);
    return download(argv[1], outputPath);
}
