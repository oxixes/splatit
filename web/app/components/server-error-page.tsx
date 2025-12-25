import { ServerStatusError } from "~/lib/server-status";
import { ServerOff, AlertTriangle, XCircle, ShieldAlert } from "lucide-react";
import { Button } from "~/components/ui/button";
import { useState } from "react";

interface ServerErrorPageProps {
  error: ServerStatusError;
  apiUrl: string;
  onOverride?: () => void;
}

export function ServerErrorPage({ error, apiUrl, onOverride }: ServerErrorPageProps) {
  const [showWarning, setShowWarning] = useState(false);
  const getErrorDetails = () => {
    switch (error.type) {
      case "unreachable":
        return {
          icon: <ServerOff className="h-24 w-24 text-orange-400" />,
          title: "Cannot Connect to Server",
          code: "ERR_CONNECTION_FAILED",
          description:
            "The Management UI server is not reachable. Please check if the server is running and the API URL is correct.",
          badgeClass: "bg-orange-500/20 text-orange-400 border border-orange-500/30",
        };
      case "incompatible":
        return {
          icon: <AlertTriangle className="h-24 w-24 text-yellow-400" />,
          title: "Incompatible Server Version",
          code: "ERR_VERSION_MISMATCH",
          description:
            "The server version is not compatible with this Management UI. Please update either the server or the UI to matching versions.",
          badgeClass: "bg-yellow-500/20 text-yellow-400 border border-yellow-500/30",
        };
      case "down":
        return {
          icon: <XCircle className="h-24 w-24 text-red-400" />,
          title: "Server is Down",
          code: "ERR_SERVER_DOWN",
          description: error.details
            ? `The server reported: ${error.details}`
            : "The server is currently experiencing issues. Please try again later or contact your system administrator.",
          badgeClass: "bg-red-500/20 text-red-400 border border-red-500/30",
        };
    }
  };

  const details = getErrorDetails();

  const handleReload = () => {
    window.location.reload();
  };

  const handleOverride = () => {
    if (error.type === "incompatible" && onOverride) {
      if (!showWarning) {
        setShowWarning(true);
      } else {
        onOverride();
      }
    }
  };

  return (
    <div className="min-h-screen bg-slate-950 flex items-center justify-center p-4">
      <div className="max-w-2xl w-full">
        <div className="bg-slate-900 border border-slate-800 rounded-lg shadow-2xl p-8 md:p-12">
          {/* Icon */}
          <div className="flex justify-center mb-6">{details.icon}</div>

          {/* Error Code */}
          <div className="text-center mb-4">
            <span className={`inline-block px-4 py-1 rounded-full text-sm font-mono font-semibold ${details.badgeClass}`}>
              {details.code}
            </span>
          </div>

          {/* Title */}
          <h1 className="text-3xl md:text-4xl font-bold text-center mb-4 text-slate-100">
            {details.title}
          </h1>

          {/* Description */}
          <p className="text-center text-slate-400 mb-6 text-lg">
            {details.description}
          </p>

          {/* Technical Details */}
          <div className="bg-slate-950 rounded-lg p-4 mb-6 border border-slate-800">
            <h3 className="font-semibold text-sm text-slate-300 mb-2">
              Technical Details:
            </h3>
            <div className="space-y-2">
              <div className="flex flex-col sm:flex-row sm:items-center gap-1 sm:gap-2">
                <span className="text-xs font-mono text-slate-500">
                  API URL:
                </span>
                <span className="text-xs font-mono text-slate-300 break-all">
                  {apiUrl}
                </span>
              </div>
              {error.details && (
                <div className="flex flex-col gap-1">
                  <span className="text-xs font-mono text-slate-500">
                    Error:
                  </span>
                  <span className="text-xs font-mono text-slate-300">
                    {error.details}
                  </span>
                </div>
              )}
            </div>
          </div>

          {/* Override Warning for Incompatible Version */}
          {error.type === "incompatible" && showWarning && (
            <div className="bg-red-500/10 border border-red-500/30 rounded-lg p-4 mb-6">
              <div className="flex items-start gap-3">
                <ShieldAlert className="h-5 w-5 text-red-400 flex-shrink-0 mt-0.5" />
                <div className="flex-1">
                  <h4 className="font-semibold text-sm text-red-400 mb-1">
                    Warning: Incompatible Version
                  </h4>
                  <p className="text-xs text-slate-400">
                    Using an incompatible server version may cause unexpected behavior,
                    data corruption, or application crashes. Features may not work correctly
                    and you may experience errors. Proceed at your own risk.
                  </p>
                </div>
              </div>
            </div>
          )}

          {/* Actions */}
          <div className="flex flex-col sm:flex-row gap-3 justify-center">
            <Button onClick={handleReload} size="lg" className="w-full sm:w-auto">
              Try Again
            </Button>
            {error.type === "incompatible" && onOverride && (
              <Button
                variant={showWarning ? "destructive" : "secondary"}
                size="lg"
                className="w-full sm:w-auto"
                onClick={handleOverride}
              >
                {showWarning ? "Continue Anyway" : "Override Version Check"}
              </Button>
            )}
            <Button
              variant="outline"
              size="lg"
              className="w-full sm:w-auto"
              onClick={() =>
                window.open(
                  "https://github.com/oxixes/splatoon_server_cpp",
                  "_blank"
                )
              }
            >
              View Documentation
            </Button>
          </div>

          {/* Footer */}
          <div className="mt-8 pt-6 border-t border-slate-800">
            <p className="text-center text-xs text-slate-500">
              SplatIt Server Management UI • If this problem persists, please check your
              configuration file
            </p>
          </div>
        </div>

        {/* Cloudflare-style footer */}
        <div className="mt-6 text-center">
          <p className="text-sm text-slate-400">
            This page is powered by{" "}
            <span className="font-semibold text-slate-300">SplatIt Server</span>
          </p>
        </div>
      </div>
    </div>
  );
}

