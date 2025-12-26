import type { ReactNode } from "react";
import { useServerStatus } from "~/hooks/useServerStatus";
import { useConfig } from "~/contexts/AppConfigContext";
import { ServerErrorPage } from "~/components/server-error-page";
import { ServerStatusLoading } from "~/components/server-status-loading";
import { useState, useEffect } from "react";
import { checkServerStatus, ServerStatusError } from "~/lib/server-status";

interface ServerStatusGuardProps {
  children: ReactNode;
}

const VERSION_OVERRIDE_KEY = "splatit_version_override";
const PING_INTERVAL = 30000; // 30 seconds

/**
 * Component that checks server status before rendering children
 * Shows error page if server is unreachable, incompatible, or down
 * Periodically pings the server to detect connection loss
 */
export function ServerStatusGuard({ children }: ServerStatusGuardProps) {
  const { config } = useConfig();
  const { status, loading, error } = useServerStatus();
  const [versionOverride, setVersionOverride] = useState(false);
  const [pingError, setPingError] = useState<ServerStatusError | null>(null);

  // Check for version override in localStorage on mount
  useEffect(() => {
    const override = localStorage.getItem(VERSION_OVERRIDE_KEY);
    if (override === "true") {
      setVersionOverride(true);
    }
  }, []);

  // Periodic ping to check if server is still reachable
  useEffect(() => {
    // Only start ping if initial load was successful or override is active
    if (!loading && (status || versionOverride)) {
      const pingServer = async () => {
        try {
          await checkServerStatus(config);
          // Server is still reachable, clear any ping errors
          setPingError(null);
        } catch (err) {
          if (err instanceof ServerStatusError) {
            // Only show error for unreachable or down, not for version mismatch if override is active
            if (err.type === "incompatible" && versionOverride) {
              // Ignore version mismatch errors when override is active
              setPingError(null);
            } else {
              setPingError(err);
            }
          }
        }
      };

      // Set up interval for periodic ping
      const intervalId = setInterval(pingServer, PING_INTERVAL);

      // Cleanup on unmount
      return () => clearInterval(intervalId);
    }
  }, [config, loading, status, versionOverride]);

  // Show loading state while checking
  if (loading) {
    return <ServerStatusLoading />;
  }

  // Handle override for incompatible version
  const handleOverride = () => {
    localStorage.setItem(VERSION_OVERRIDE_KEY, "true");
    setVersionOverride(true);
    setPingError(null); // Clear any ping errors when override is activated
  };

  // Check for ping errors first (connection lost after initial load)
  if (pingError) {
    return <ServerErrorPage error={pingError} apiUrl={config.apiUrl} onOverride={handleOverride} />;
  }

  // Show error page if there's an error (except incompatible version with override)
  if (error) {
    // Allow override for incompatible version
    if (error.type === "incompatible" && versionOverride) {
      // Override is active, allow access
      if (status) {
        return <>{children}</>;
      }
    }

    return <ServerErrorPage error={error} apiUrl={config.apiUrl} onOverride={handleOverride} />;
  }

  // Server is OK, render children
  if (status && status.status === "ok") {
    return <>{children}</>;
  }

  // Fallback (shouldn't reach here)
  return <ServerStatusLoading />;
}

