#include <drogon/drogon.h>
#include <atomic>
#include <vector>
#include <string>

using namespace drogon;

struct Backend {
    std::string host;
    uint16_t port;
};

class LoadBalancer {
public:
    explicit LoadBalancer(std::vector<Backend> backends)
        : backends_(std::move(backends)) {}

    Backend next() {
        size_t idx = counter_.fetch_add(1, std::memory_order_relaxed);
        return backends_[idx % backends_.size()];
    }

private:
    std::vector<Backend> backends_;
    std::atomic<size_t> counter_{0};
};

int main() {
    // ---- Config ----
    // listen port -> backends (round-robin)
    const uint16_t listenPort = 8080;
    std::vector<Backend> backends = {
        {"127.0.0.1", 9001},
        {"127.0.0.1", 9002},
    };

    auto lb = std::make_shared<LoadBalancer>(backends);

    // Persistent HTTP client (keep-alive to backends)
    auto client = HttpClient::newHttpClient("http://127.0.0.1");

    // Catch-all handler: forward everything
    app().registerHandler(
        "/{path}",
        [lb, client](const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback,
                     const std::string &path) {
            auto backend = lb->next();
            std::string url = "http://" + backend.host + ":" +
                              std::to_string(backend.port) + "/" + path;

            // Preserve query string
            if (!req->query().empty()) url += "?" + req->query();

            // Build outbound request preserving method, headers, body
            auto out = HttpRequest::newHttpRequest();
            out->setMethod(req->method());
            out->setPath(url);
            for (auto &h : req->headers()) {
                // Skip hop-by-hop headers
                if (h.first == "Host" || h.first == "Connection" ||
                    h.first == "Content-Length") continue;
                out->addHeader(h.first, h.second);
            }
            out->setBody(std::string(req->body()));

            LOG_INFO << "LB: " << req->methodString() << " " << req->path()
                     << " -> " << backend.host << ":" << backend.port;

            client->sendRequest(
                out,
                [callback](ReqResult result, const HttpResponsePtr &resp) {
                    if (result != ReqResult::Ok || !resp) {
                        auto err = HttpResponse::newHttpResponse();
                        err->setStatusCode(k502BadGateway);
                        err->setBody("Bad Gateway");
                        callback(err);
                        return;
                    }
                    callback(resp);
                },
                5.0 // timeout seconds
            );
        },
        {Get, Post, Put, Delete, Patch, Head, Options});

    LOG_INFO << "Load balancer listening on :" << listenPort;
    app().addListener("0.0.0.0", listenPort).run();
}
