#include "BrowserClient.h"
#include "WallpaperEngine/Logging/Log.h"

#include <cstdlib>

using namespace WallpaperEngine::WebBrowser::CEF;

BrowserClient::BrowserClient (CefRefPtr<CefRenderHandler> ptr) : m_renderHandler (std::move (ptr)) { }

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler () { return m_renderHandler; }

bool BrowserClient::OnConsoleMessage (
    CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message, const CefString& source, int line
) {
    static const bool enabled = std::getenv ("LWE_WEB_DEBUG") != nullptr;

    if (enabled) {
	sLog.out ("web console: ", message.ToString (), " (", source.ToString (), ":", line, ")");
    }

    return false; // let Chromium apply its own logging as well
}

void BrowserClient::OnLoadError (
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText,
    const CefString& failedUrl
) {
    // ERR_ABORTED is a navigation the page cancelled itself
    if (errorCode == ERR_ABORTED) {
	return;
    }

    sLog.error ("web: failed to load ", failedUrl.ToString (), ": ", errorText.ToString (), " (", errorCode, ")");
}

void BrowserClient::OnRenderProcessTerminated (
    CefRefPtr<CefBrowser> browser, TerminationStatus status, int error_code, const CefString& error_string
) {
    sLog.error ("web: renderer process ended (status ", static_cast<int> (status), ", code ", error_code, "): ",
		error_string.ToString ());
}
