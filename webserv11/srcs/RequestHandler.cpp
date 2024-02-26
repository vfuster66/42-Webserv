#include "../includes/RequestHandler.hpp"

RequestHandler::RequestHandler() : current_port(-1)
{
}

void RequestHandler::setServerConfigs(const std::vector<ServerConfig>& configs)
{
    serverConfigs = configs;
}

void RequestHandler::setServerPort(int port)
{
    current_port = port;
}

int RequestHandler::extractPortFromHostHeader(const std::string& hostHeader)
{
    size_t colonPos = hostHeader.find_last_of(':');
    if (colonPos != std::string::npos)
    {
        std::istringstream portStrStream(hostHeader.substr(colonPos + 1));
        int port;
        portStrStream >> port;
        return port;
    }
    return 80;
}

ServerConfig RequestHandler::getServerConfigForPort(int port)
{
    for (std::vector<ServerConfig>::iterator it = serverConfigs.begin(); it != serverConfigs.end(); ++it)
    {
        if (it->port == port)
        {
            return *it;
        }
    }
    std::ostringstream errMsg;
    std::cerr << errMsg.str() << std::endl;
    throw std::runtime_error(errMsg.str());
}


bool RequestHandler::isValidRequest(const HttpRequest& request)
{
    if(request.method != "GET" && request.method != "POST" && request.method != "DELETE")
    {
        LOG_ERROR("RequestHandler::isValidRequest - Méthode HTTP non supportée: " + request.method);
        return false;
    }
    LOG_INFO("RequestHandler::isValidRequest - Requête valide avec la méthode: " + request.method);
    return true;
}


HttpResponse RequestHandler::handleRequest(const HttpRequest& request)
{

    if (!isValidRequest(request)) {
        HttpResponse response;
        response.httpVersion = "HTTP/1.1";
        response.statusCode = 400;
        response.statusMessage = "Bad Request";
        response.body = loadErrorPage(400);
        return response;
    }
    if (isCgiRequest(request.uri))
    {
        return handleCgiRequest(request);
    }
    else if (request.method == "GET")
    {
        return handleGetRequest(request);
    }
    else if (request.method == "POST")
    {
        return handlePostRequest(request);
    } 
    else if (request.method == "DELETE")
    {
        return handleDeleteRequest(request);
    }
    else
    {
        HttpResponse response;
        response.httpVersion = "HTTP/1.1";
        response.statusCode = 405;
        response.statusMessage = "Method Not Allowed";
        response.headers.insert(std::make_pair(std::string("Allow"), std::string("GET, POST, DELETE")));
        response.body = loadErrorPage(405);
        response.headers.insert(std::make_pair(std::string("Content-Type"), std::string("text/html")));
        std::ostringstream contentLengthStream;
        contentLengthStream << response.body.length();
        response.headers.insert(std::make_pair(std::string("Content-Length"), contentLengthStream.str()));
        return response;
    }

}

HttpResponse RequestHandler::handleCgiRequest(const HttpRequest& request)
{
    LOG_INFO("handleCgiRequest -> Starting CGI request handling for URI: " + request.uri);

    std::string scriptPath = extractScriptName(request.uri);
    std::string queryString = extractQueryString(request.uri);
    LOG_INFO("handleCgiRequest -> Extracted script path: " + scriptPath + ", Query string: " + queryString);

    std::map<std::string, std::string> cgiEnv;
    cgiEnv.insert(std::make_pair("QUERY_STRING", queryString));
    cgiEnv.insert(std::make_pair("REQUEST_METHOD", request.method));
    cgiEnv.insert(std::make_pair("SCRIPT_NAME", scriptPath));
    cgiEnv.insert(std::make_pair("CONTENT_LENGTH", request.getHeader("Content-Length")));
    cgiEnv.insert(std::make_pair("CONTENT_TYPE", request.getHeader("Content-Type")));
    LOG_INFO("handleCgiRequest -> CGI environment prepared");

    CgiHandler cgiHandler(scriptPath, cgiEnv);
    LOG_INFO("handleCgiRequest -> CGI handler instance created");

    std::string cgiOutput = cgiHandler.execute();
    std::ostringstream ss;
    ss << cgiOutput.length();
    std::string outputLength = ss.str();

    LOG_INFO("handleCgiRequest -> CGI script executed, output length: " + outputLength);

    HttpResponse response;
    response.httpVersion = "HTTP/1.1";
    response.statusCode = 200;
    response.statusMessage = "OK";
    response.body = cgiOutput;
    response.headers.insert(std::make_pair("Content-Type", "text/html"));
    std::ostringstream contentLengthStream;
    contentLengthStream << cgiOutput.size();
    response.headers.insert(std::make_pair("Content-Length", contentLengthStream.str()));
    LOG_INFO("handleCgiRequest -> HTTP response constructed based on CGI output");

    return response;
}

HttpResponse RequestHandler::handleGetRequest(const HttpRequest& request)
{
    HttpResponse response;

    int port = extractPortFromHostHeader(request.headers.find("Host")->second);
    
    try
    {
        const ServerConfig& serverConfig = getServerConfigForPort(port);

        std::string rootPath = serverConfig.root;
        if (!rootPath.empty() && (rootPath[rootPath.size() - 1] == ';' || rootPath[rootPath.size() - 1] == '/'))
        {
            rootPath.erase(rootPath.size() - 1);
        }

        std::string filePath = rootPath + request.uri;

        if (!filePath.empty() && filePath[filePath.size() - 1] == '/')
        {
            filePath += serverConfig.index.empty() ? "index.html" : serverConfig.index.front();
        }

        std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary);
        if (file)
        {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            response.httpVersion = "HTTP/1.1";
            response.statusCode = 200;
            response.statusMessage = "OK";
            response.body = content;
            response.headers["Content-Type"] = determineMimeType(filePath);
        }
        else
        {
            return generateNotFoundResponse();
        }
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "Erreur: " << e.what() << std::endl;
        return generateInternalServerErrorResponse();
    }

    std::ostringstream oss;
    oss << response.body.size();
    response.headers["Content-Length"] = oss.str();

    return response;
}



HttpResponse RequestHandler::generateInternalServerErrorResponse()
{
    HttpResponse response;
    response.httpVersion = "HTTP/1.1";
    response.statusCode = 500;
    response.statusMessage = "Internal Server Error";
    response.body = loadErrorPage(500);
    return response;
}

HttpResponse RequestHandler::generateNotFoundResponse()
{
    HttpResponse response;
    response.httpVersion = "HTTP/1.1";
    response.statusCode = 404;
    response.statusMessage = "Not Found";
    
    std::string errorPageContent = "<html><head><title>404 Not Found</title></head>"
                                    "<body><h1>404 Not Found</h1>"
                                    "<p>The requested URL was not found on this server.</p></body></html>";
    response.body = errorPageContent;

    std::ostringstream contentLengthStream;
    contentLengthStream << errorPageContent.size();
    response.headers["Content-Length"] = contentLengthStream.str();

    response.headers["Content-Type"] = "text/html";

    return response;
}


HttpResponse RequestHandler::handleDeleteRequest(const HttpRequest& request)
{
    HttpResponse response;

    std::string filePath = "www" + request.uri;

    std::ifstream file(filePath.c_str());
    if (file) {
        file.close();

        if (std::remove(filePath.c_str()) == 0)
        {
            response.httpVersion = "HTTP/1.1";
            response.statusCode = 204;
            response.statusMessage = "No Content";
            
        }
        else
        {
            response.httpVersion = "HTTP/1.1";
            response.statusCode = 500;
            response.statusMessage = "Internal Server Error";
            response.body = loadErrorPage(500);
            response.headers["Content-Type"] = "text/html";

        }
    }
    else
    {
        response.httpVersion = "HTTP/1.1";
        response.statusCode = 404;
        response.statusMessage = "Not Found";
        response.body = loadErrorPage(404);
    }

    if (!response.body.empty())
    {
        std::ostringstream oss;
        oss << response.body.length();
        response.headers["Content-Length"] = oss.str();
    }

    return response;
}


HttpResponse RequestHandler::handlePostRequest(const HttpRequest& request)
{
    HttpResponse response;

    if (isMultipartFormData(request))
    {
        std::string contentType = request.headers.find("Content-Type")->second;
        std::string boundary = getBoundary(contentType);

        if (!boundary.empty())
        {
            std::vector<FilePart> files;
            parseMultipartFormData(request.body, boundary, files);

            for (size_t i = 0; i < files.size(); ++i)
            {
                saveFile(files[i].fileContent, files[i].fileName);
            }

            response.httpVersion = "HTTP/1.1";
            response.statusCode = 200;
            response.statusMessage = "OK";
            response.body = "<html><body><h1>File(s) Uploaded Successfully</h1></body></html>";
            response.headers["Content-Type"] = "text/html";
        }
        else
        {
            response.httpVersion = "HTTP/1.1";
            response.statusCode = 400;
            response.statusMessage = "Bad Request";
            response.body = loadErrorPage(400);
            response.headers["Content-Type"] = "text/html";
        }
    }
    else
    {
        std::map<std::string, std::string> formData = parseFormData(request.body);

        bool success = performAction(formData);

        if (success)
        {
            response.httpVersion = "HTTP/1.1";
            response.statusCode = 200;
            response.statusMessage = "OK";
            response.body = "<html><body><h1>Success</h1><p>Your data was processed successfully.</p></body></html>";
            response.headers["Content-Type"] = "text/html";
        }
        else
        {
            response.httpVersion = "HTTP/1.1";
            response.statusCode = 400;
            response.statusMessage = "Bad Request";
            response.body = loadErrorPage(400);
            response.headers["Content-Type"] = "text/html";
        }

        std::ostringstream oss;
        oss << response.body.length();
        response.headers["Content-Length"] = oss.str();
    }

    return response;
}

std::map<std::string, std::string> RequestHandler::parseFormData(const std::string& body)
{
    std::map<std::string, std::string> formData;
    std::istringstream dataStream(body);
    std::string pair;

    while (std::getline(dataStream, pair, '&'))
    {
        std::string::size_type pos = pair.find('=');
        if (pos != std::string::npos)
        {
            std::string key = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);

            for (std::string::iterator it = value.begin(); it != value.end(); ++it)
            {
                if (*it == '+') *it = ' ';
            }

            formData[key] = urlDecode(value);
        }
    }
    return formData;
}


bool RequestHandler::performAction(const std::map<std::string, std::string>& formData)
{
    std::ofstream outFile("form_data.txt", std::ios::app);
    if (!outFile.is_open())
    {
        std::cerr << "Erreur lors de l'ouverture du fichier pour écriture." << std::endl;
        return false;
    }

    for (std::map<std::string, std::string>::const_iterator it = formData.begin(); it != formData.end(); ++it)
    {
        outFile << it->first << ": " << it->second << std::endl;
    }

    outFile.close();
    return true;
}




std::string urlDecode(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            std::string hexStr = str.substr(i + 1, 2);
            char decodedChar = static_cast<char>(std::strtol(hexStr.c_str(), NULL, 16));
            result.push_back(decodedChar);
            i += 2; // Skip the next two characters
        } else {
            result.push_back(str[i]);
        }
    }
    return result;
}


//parsing
void RequestHandler::parseRequestLine(const std::string& line, HttpRequest& request)
{
    LOG_INFO("parseRequestLine -> Début de l'analyse de la ligne de requête");

    std::istringstream stream(line);
    stream >> request.method >> request.uri >> request.httpVersion;

    std::ostringstream logMessage;
    logMessage << "parseRequestLine -> Méthode : " << request.method
            << ", URI : " << request.uri
            << ", Version HTTP : " << request.httpVersion;
    LOG_INFO(logMessage.str());

    LOG_INFO("parseRequestLine -> Fin de l'analyse de la ligne de requête");
}

void RequestHandler::parseHeaders(std::istringstream& stream, HttpRequest& request)
{
    LOG_INFO("parseHeaders -> Début de l'analyse des en-têtes HTTP");

    std::string line;
    while (std::getline(stream, line) && line != "\r")
    {
        std::string::size_type delimiterPos = line.find(": ");
        if (delimiterPos != std::string::npos)
        {
            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 2);
            if (!value.empty() && value[value.length() - 1] == '\r')
            {
                value.erase(value.length() - 1);
            }
            request.headers[key] = value;

            std::ostringstream logMessage;
            logMessage << "parseHeaders -> En-tête trouvé : " << key << " : " << value;
            LOG_INFO(logMessage.str());
        }
    }

    LOG_INFO("parseHeaders -> Fin de l'analyse des en-têtes HTTP");
}

void RequestHandler::parseBody(HttpRequest& request) {
    if (request.headers["Content-Type"] == "application/x-www-form-urlencoded") {
        std::istringstream dataStream(request.body);
        std::string pair;
        while (std::getline(dataStream, pair, '&')) {
            std::string::size_type pos = pair.find('=');
            if (pos != std::string::npos) {
                std::string key = pair.substr(0, pos);
                std::string value = pair.substr(pos + 1);
                request.formData[key] = urlDecode(value);
            }
        }
    }
    else if (isMultipartFormData(request)) {
        std::string contentType = request.headers["Content-Type"];
        std::string boundary = getBoundary(contentType);
        
        if (!boundary.empty()) {
            std::vector<FilePart> files;
            parseMultipartFormData(request.body, boundary, files);
        }
    }
}

HttpRequest RequestHandler::parseRequest(const std::string& requestText)
{
    LOG_INFO("parseRequest -> Début du parsing de la requête");

    HttpRequest request;
    std::istringstream stream(requestText);
    std::string requestLine;
    std::getline(stream, requestLine);

    LOG_INFO("parseRequest -> Parsing de la ligne de requête");
    parseRequestLine(requestLine, request);

    LOG_INFO("parseRequest -> Parsing des en-têtes");
    parseHeaders(stream, request);

    LOG_INFO("parseRequest -> Parsing du corps");
    parseBody(request);

    LOG_INFO("parseRequest -> Parsing de la requête terminé");

    return request;
}

//extensions
std::string RequestHandler::determineMimeType(const std::string& filePath)
{
    std::string mimeType = "text/plain";

    if (filePath.find(".html") != std::string::npos)
        mimeType = "text/html";
    else if (filePath.find(".css") != std::string::npos)
        mimeType = "text/css";
    else if (filePath.find(".js") != std::string::npos)
        mimeType = "application/javascript";
    else if (filePath.find(".png") != std::string::npos)
        mimeType = "image/png";
    else if (filePath.find(".jpg") != std::string::npos || filePath.find(".jpeg") != std::string::npos)
        mimeType = "image/jpeg";
    else if (filePath.find(".gif") != std::string::npos)
        mimeType = "image/gif";
    else if (filePath.find(".svg") != std::string::npos)
        mimeType = "image/svg+xml";
    else if (filePath.find(".mp4") != std::string::npos)
        mimeType = "video/mp4";
    else if (filePath.find(".webm") != std::string::npos)
        mimeType = "video/webm";
    else if (filePath.find(".mp3") != std::string::npos)
        mimeType = "audio/mpeg";
    else if (filePath.find(".wav") != std::string::npos)
        mimeType = "audio/wav";
    else if (filePath.find(".pdf") != std::string::npos)
        mimeType = "application/pdf";
    else if (filePath.find(".docx") != std::string::npos)
        mimeType = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    else if (filePath.find(".json") != std::string::npos)
        mimeType = "application/json";
    else if (filePath.find(".xml") != std::string::npos)
        mimeType = "application/xml";

    LOG_INFO("determineMimeType -> Type MIME déterminé pour " + filePath + " : " + mimeType);
    
    return mimeType;
}

// erreurs
std::string RequestHandler::loadErrorPage(int statusCode)
{
    std::ostringstream filePathStream;
    filePathStream << "www/errors/" << statusCode << ".html";
    std::string filePath = filePathStream.str();

    LOG_INFO("loadErrorPage -> Tentative de chargement de la page d'erreur : " + filePath);

    std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary);
    if (file)
    {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        LOG_INFO("loadErrorPage -> Page d'erreur chargée avec succès : " + filePath);
        return content;
    }
    else
    {
        LOG_ERROR("loadErrorPage -> Échec du chargement de la page d'erreur, fichier non trouvé : " + filePath);
        std::ostringstream errorMessage;
        errorMessage << "<html><body><h1>Error " << statusCode << "</h1></body></html>";
        return errorMessage.str();
    }
}


// upload
bool RequestHandler::isMultipartFormData(const HttpRequest& request)
{
    std::string contentType;
    std::map<std::string, std::string>::const_iterator it = request.headers.find("Content-Type");

    if (it != request.headers.end())
    {
        contentType = it->second;
        LOG_INFO("isMultipartFormData -> Content-Type trouvé : " + contentType);
    }
    else
    {
        LOG_WARNING("isMultipartFormData -> Aucun Content-Type trouvé dans les en-têtes de la requête.");
    }

    bool isMultipart = contentType.find("multipart/form-data") != std::string::npos;

    if (isMultipart)
    {
        LOG_INFO("isMultipartFormData -> La requête est de type multipart/form-data.");
    }
    else
    {
        LOG_INFO("isMultipartFormData -> La requête n'est pas de type multipart/form-data.");
    }

    return isMultipart;
}

std::string RequestHandler::getBoundary(const std::string& contentType)
{
    LOG_INFO("getBoundary -> Début de getBoundary avec contentType: " + contentType);

    std::string::size_type pos = contentType.find("boundary=");
    if (pos != std::string::npos)
    {
        std::string boundary = contentType.substr(pos + 9);
        LOG_INFO("getBoundary -> Boundary trouvé: " + boundary);
        return boundary;
    }

    LOG_WARNING("getBoundary -> Aucun boundary trouvé dans contentType");
    return "";
}

void RequestHandler::parseMultipartFormData(const std::string& body, const std::string& boundary, std::vector<FilePart>& files)
{
    LOG_INFO("parseMultipartFormData -> Début de parseMultipartFormData");

    std::string delimiter = "--" + boundary;
    std::string endDelimiter = delimiter + "--";
    std::size_t pos = 0, endPos = 0;

    while ((pos = body.find(delimiter, pos)) != std::string::npos)
    {
        endPos = body.find(delimiter, pos + delimiter.length());

        if (endPos == std::string::npos)
            break;

        std::string part = body.substr(pos + delimiter.length(), endPos - pos - delimiter.length());
        FilePart filePart = extractFilePart(part);

        if (!filePart.fileName.empty())
        {
            LOG_INFO("parseMultipartFormData -> Fichier extrait: " + filePart.fileName);
            files.push_back(filePart);
        }

        pos = endPos + delimiter.length();
    }

    LOG_INFO("parseMultipartFormData -> Fin de parseMultipartFormData");
}

void RequestHandler::saveFile(const std::string& content, const std::string& fileName)
{
    std::string filePath = "/home/vfuster-/42-Webserv/webserv4/uploads/" + fileName;

    std::ofstream file(filePath.c_str(), std::ios::out | std::ios::binary);
    if (file.is_open())
    {
        file.write(content.c_str(), content.size());
        file.close();
        LOG_INFO("saveFile -> Fichier sauvegardé avec succès : " + filePath);
    }
    else
    {
        LOG_ERROR("saveFile -> Erreur lors de l'ouverture du fichier pour écriture : " + filePath);
    }
}

FilePart RequestHandler::extractFilePart(const std::string& part)
{
    FilePart filePart;
    std::istringstream partStream(part);
    std::string line;
    std::string contentDisposition;
    bool contentStart = false;

    while (std::getline(partStream, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r')
        {
            line.erase(line.size() - 1);
        }

        if (line.empty())
        {
            contentStart = true;
            LOG_INFO("extractFilePart -> Début du contenu du fichier détecté.");
            continue;
        }

        if (!contentStart)
        {
            if (line.find("Content-Disposition:") != std::string::npos)
            {
                contentDisposition = line;
                LOG_INFO("extractFilePart -> Content-Disposition trouvé : " + line);
                std::size_t filenamePos = line.find("filename=\"");
                if (filenamePos != std::string::npos)
                {
                    filenamePos += 10;
                    std::size_t filenameEnd = line.find("\"", filenamePos);
                    if (filenameEnd != std::string::npos)
                    {
                        filePart.fileName = line.substr(filenamePos, filenameEnd - filenamePos);
                        LOG_INFO("extractFilePart -> Nom du fichier extrait : " + filePart.fileName);
                    }
                }
            }
        }
        else
        {
            filePart.fileContent += line + "\n";
        }
    }

    if (!filePart.fileContent.empty())
    {
        filePart.fileContent.erase(filePart.fileContent.size() - 1);
    }

    LOG_INFO("extractFilePart -> Extraction du fichier terminée. Nom du fichier : " + filePart.fileName);
    return filePart;
}

#include <algorithm>

// CGI
bool RequestHandler::isCgiRequest(const std::string& uri) 
{
    LOG_INFO("isCgiRequest -> Starting CGI request check for URI: " + uri);
    for (size_t i = 0; i < sizeof(cgiConfigs) / sizeof(cgiConfigs[0]); ++i)
    {
        if (uri.find(cgiConfigs[i].extension) != std::string::npos)
        {
            LOG_INFO("isCgiRequest -> URI identified as a CGI request: " + uri + " Handler: " + cgiConfigs[i].handlerPath);
            return true;
        }
    }

    LOG_INFO("isCgiRequest -> URI not identified as a CGI request: " + uri);
    return false;
}

std::string RequestHandler::extractQueryString(const std::string& uri)
{
    LOG_INFO("extractQueryString -> Starting query string extraction for URI: " + uri);
    size_t pos = uri.find("?");
    std::string queryString = "";

    if (pos != std::string::npos)
    {
        queryString = uri.substr(pos + 1);
        LOG_INFO("extractQueryString -> Extracted query string: " + queryString);
    }
    else
    {
        LOG_INFO("extractQueryString -> No query string found in URI");
    }

    return queryString;
}

std::string RequestHandler::extractScriptName(const std::string& uri)
{
    LOG_INFO("extractScriptName -> Starting script name extraction for URI: " + uri);
    size_t pos = uri.find("?");
    std::string scriptName = "";

    if (pos != std::string::npos)
    {
        scriptName = uri.substr(0, pos);
    }
    else
    {
        scriptName = uri;
    }

    // Enlever le '/' initial si présent
    if (!scriptName.empty() && scriptName[0] == '/') {
        scriptName.erase(0, 1); // Enlevez le premier caractère
    }

    LOG_INFO("extractScriptName -> Extracted script name: " + scriptName);

    return scriptName;
}



