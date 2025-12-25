import { useState, useEffect } from "react";
import { useConfig } from "~/contexts/AppConfigContext";
import { checkServerStatus, ServerStatusError } from "~/lib/server-status";
import type { ServerStatus } from "~/lib/server-status";

interface ServerStatusState {
  status: ServerStatus | null;
  loading: boolean;
  error: ServerStatusError | null;
}

/**
 * Hook to check server status on mount
 */
export function useServerStatus() {
  const { config, loading: configLoading } = useConfig();
  const [state, setState] = useState<ServerStatusState>({
    status: null,
    loading: true,
    error: null,
  });

  useEffect(() => {
    if (configLoading) {
      return;
    }

    const checkStatus = async () => {
      try {
        const status = await checkServerStatus(config);
        setState({
          status,
          loading: false,
          error: null,
        });
      } catch (error) {
        if (error instanceof ServerStatusError) {
          // For incompatible version errors, still store the status
          // This allows the override to work properly
          let statusData = null;
          if (error.type === "incompatible") {
            try {
              const response = await fetch(`${config.apiUrl}/status`);
              if (response.ok) {
                statusData = await response.json();
              }
            } catch {
              // Ignore fetch errors here
            }
          }

          setState({
            status: statusData,
            loading: false,
            error,
          });
        } else {
          setState({
            status: null,
            loading: false,
            error: new ServerStatusError(
              "Unknown error",
              "unreachable",
              String(error)
            ),
          });
        }
      }
    };

    checkStatus();
  }, [config, configLoading]);

  return state;
}

