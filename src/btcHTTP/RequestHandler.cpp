
#include "btcHTTP/RequestHandler.h"
#include "btcHTTP/MimeTypes.h"
#include "btcHTTP/Reply.h"
#include "btcHTTP/Request.h"
#include "btcHTTP/Method.h"
#include "btcHTTP/RPC.h"

#include <fstream>
#include <sstream>
#include <string>
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost;
using namespace json_spirit;

// This method, "dirty", dirties the memory cache of all get requests to force a reload.
class DirtyDocCache : public Method
{
public:
    DirtyDocCache(RequestHandler& delegate) : _delegate(delegate) {}
    virtual Value operator()(const Array& params, bool fHelp) {
        
        cout << _delegate.getDocCacheStats(2) << flush;
        _delegate.clearDocCache();
        return Value::null;
    }    
private:
    RequestHandler& _delegate;
};

RequestHandler::RequestHandler(const string& doc_root) : _doc_root(doc_root) {
    registerMethod(method_ptr(new DirtyDocCache(*this)));
}

void RequestHandler::registerMethod(method_ptr method) {
    _methods[method->name()] = method;
}

void RequestHandler::unregisterMethod(const string name) {
    _methods.erase(name);
}

void RequestHandler::handleGET(const Request& req, Reply& rep) {
    // Decode url to path.
    string request_path;
    if (!urlDecode(req.uri, request_path)) {
        rep = Reply::stock_reply(Reply::bad_request);
        return;
    }
    
    // Request path must be absolute and not contain "..".
    if (request_path.empty() || request_path[0] != '/' || request_path.find("..") != string::npos) {
        rep = Reply::stock_reply(Reply::bad_request);
        return;
    }
    
    // If path ends in slash (i.e. is a directory) then add "index.html".
    if (request_path[request_path.size() - 1] == '/') {
        request_path += "index.html";
    }    
    
    // Determine the file extension.
    size_t last_slash_pos = request_path.find_last_of("/");
    size_t last_dot_pos = request_path.find_last_of(".");
    string extension;
    if (last_dot_pos == string::npos) {
        request_path += "/index.html";
        last_slash_pos = request_path.find_last_of("/");
        last_dot_pos = request_path.find_last_of(".");
    }
    if (last_dot_pos > last_slash_pos) {
        extension = request_path.substr(last_dot_pos + 1);
    }
    
    // First check the cache.
    string& content = _doc_cache[request_path];
    
    // Not cached - load and cache it
    if(content.size() == 0) {
        // Open the file to send back.
        string full_path = _doc_root + request_path;
        ifstream is(full_path.c_str(), ios::in | ios::binary);
        if (is) {
            char buf[512];
            while (is.read(buf, sizeof(buf)).gcount() > 0)
                content.append(buf, is.gcount());            
        }
        else {
            // Check if the requested resource is stored as a collection of files - we encode collections by adding a trailing _ to the extension
            string collection_name = full_path + "_";
            ifstream collection(collection_name.c_str(), ios::in | ios::binary);
            if(!collection) {
                _doc_cache.erase(request_path);
                rep = Reply::stock_reply(Reply::not_found);
                return;
            }
            
            // "collection" points to a set of files that should be concatenated
            while (!collection.eof()) {
                string path_name = "";
                getline(collection, path_name);
                
                // Construct file name.
                string full_path = _doc_root + request_path.substr(0, last_slash_pos+1) + path_name;
                ifstream part(full_path.c_str(), ios::in | ios::binary);
                if (part) {
                    char buf[512];
                    while (part.read(buf, sizeof(buf)).gcount() > 0)
                        content.append(buf, part.gcount());        
                }
                else
                    cerr << "Encountered no such file: " << full_path << ", In trying to read file collection: " << request_path << " - Ignoring" << endl;
            }
            
        }
    }
    
    // Fill out the reply to be sent to the client.
    rep.status = Reply::ok;
    rep.content = content;
    rep.headers["Content-Length"] = lexical_cast<string>(rep.content.size());
    rep.headers["Content-Type"] = MimeTypes::extension_to_type(extension);
}

void RequestHandler::handlePOST(const Request& req, Reply& rep) {
    // use the header Content-Type to lookup a suitable application
    if(req.headers.count("Content-Type")) {
        const string mime = req.headers.find("Content-Type")->second;
        if(mime == "application/json") {
            // This is a JSON RPC call - parse and execute!

            RPC rpc;
            try {
                rpc.parse(req.payload);

                // Find method
                Methods::iterator m = _methods.find(rpc.method());
                if (m == _methods.end())
                    throw RPC::error(RPC::method_not_found);
                
                try {
                    // Execute - should set  CRITICAL_BLOCK(cs_main)                    
                    rpc.execute(*(m->second));                    
                }
                catch (std::exception& e) {
                    rpc.setError(RPC::error(RPC::unknown_error, e.what()));
                }
            }
            catch (Object& err) {
                rpc.setError(err);
            }
            catch (std::exception& e) {
                rpc.setError(RPC::error(RPC::parse_error, e.what()));
            }
            // Form reply header and content
            rep.content = rpc.getContent();
            // rpc.setContent(rep.content);                    
            rep.headers["Content-Length"] = lexical_cast<string>(rep.content.size());
            rep.headers["Content-Type"] = "application/json";
            rep.status = rpc.getStatus();
            return;
        }
        rep = Reply::stock_reply(Reply::not_implemented);
        return;
    }
    rep = Reply::stock_reply(Reply::bad_request);
}

void RequestHandler::clearDocCache() {
    _doc_cache.clear();
}

std::string RequestHandler::getDocCacheStats(int level) {
    string stats;
    long size = 0;
    int entries = 0;
    ostringstream oss;
    for(DocCache::iterator i = _doc_cache.begin(); i != _doc_cache.end(); ++i) {
        oss << i->first << " : " << i->second.size() << "\n";
        size += i->second.size();
        entries++;
    }
    switch (level) {
        case 2:
            stats.append(oss.str());
        case 1:
            oss.clear();
            oss << "Entries: " << entries << " Total Size: " << size << "\n";
            stats.append(oss.str()); 
            break;
        default:
            oss.clear();
            oss << size;
            stats.append(oss.str()); 
            break;
    }
    return stats;
}



bool RequestHandler::urlDecode(const string& in, string& out) {
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%') {
            if (i + 3 <= in.size()) {
                int value = 0;
                istringstream is(in.substr(i + 1, 2));
                if (is >> hex >> value) {
                    out += static_cast<char>(value);
                    i += 2;
                }
                else {
                    return false;
                }
            }
            else {
                return false;
            }
        }
        else if (in[i] == '+') {
            out += ' ';
        }
        else {
            out += in[i];
        }
    }
    return true;
}