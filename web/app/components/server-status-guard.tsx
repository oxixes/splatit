import type { ReactNode } from "react";
import { useServerStatus } from "~/hooks/useServerStatus";
import { useConfig } from "~/contexts/AppConfigContext";
import { ServerErrorPage } from "~/components/server-error-page";
import { ServerStatusLoading } from "~/components/server-status-loading";
import { useState, useEffect } from "react";

interface ServerStatusGuardProps {
  children: ReactNode;
}

const VERSION_OVERRIDE_KEY = "splatit_version_override";

/**
 * Component that checks server status before rendering children
 * Shows error page if server is unreachable, incompatible, or down
 */
export function ServerStatusGuard({ children }: ServerStatusGuardProps) {
  const { config } = useConfig();
  const { status, loading, error } = useServerStatus();
  const [versionOverride, setVersionOverride] = useState(false);

  // Check for version override in localStorage on mount
  useEffect(() => {
    const override = localStorage.getItem(VERSION_OVERRIDE_KEY);
    if (override === "true") {
      setVersionOverride(true);
    }
  }, []);

  // Show loading state while checking
  if (loading) {
    return <ServerStatusLoading />;
  }

  // Handle override for incompatible version
  const handleOverride = () => {
    localStorage.setItem(VERSION_OVERRIDE_KEY, "true");
    setVersionOverride(true);
  };

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

