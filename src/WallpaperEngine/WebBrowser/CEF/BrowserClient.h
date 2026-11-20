#pragma once

#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"

namespace WallpaperEngine::WebBrowser::CEF {
// *************************************************************************
//! \brief Provide access to browser-instance-specific callbacks. A single
//! CefClient instance can be shared among any number of browsers.
//!
//! Besides rendering it reports what a web wallpaper does, since there is no
//! devtools window: load failures and renderer crashes always, the page's
//! console output when LWE_WEB_DEBUG is set.
// *************************************************************************
class BrowserClient : public CefClient,
		      public CefDisplayHandler,
		      public CefLoadHandler,
		      public CefRequestHandler {
public:
    explicit BrowserClient (CefRefPtr<CefRenderHandler> ptr);

    [[nodiscard]] CefRefPtr<CefRenderHandler> GetRenderHandler () override;
    [[nodiscard]] CefRefPtr<CefDisplayHandler> GetDisplayHandler () override { return this; }
    [[nodiscard]] CefRefPtr<CefLoadHandler> GetLoadHandler () override { return this; }
    [[nodiscard]] CefRefPtr<CefRequestHandler> GetRequestHandler () override { return this; }

    bool OnConsoleMessage (
	CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message, const CefString& source,
	int line
    ) override;
    void OnLoadError (
	CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText,
	const CefString& failedUrl
    ) override;
    void OnRenderProcessTerminated (
	CefRefPtr<CefBrowser> browser, TerminationStatus status, int error_code, const CefString& error_string
    ) override;

    CefRefPtr<CefRenderHandler> m_renderHandler = nullptr;

    IMPLEMENT_REFCOUNTING (BrowserClient);
};
} // namespace WallpaperEngine::WebBrowser::CEF
