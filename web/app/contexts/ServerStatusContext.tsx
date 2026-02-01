import { createContext, useContext, useState, useEffect } from "react";
import type { ReactNode } from "react";
import { useConfig } from "~/contexts/AppConfigContext";
import { createApiClient, ApiError } from "~/lib/api-client";

export interface ServerInfo {
  address: string;
  status: "online" | "offline";
  type: "account" | "boss" | "friends_auth" | "friends_secure" | "splatoon_auth" | "splatoon_secure";
  message?: string;
}

interface ServerStatusResponse {
  servers: ServerInfo[];
}

interface ServerStatusContextType {
  servers: ServerInfo[];
  loading: boolean;
  error: string | null;
  refreshing: boolean;
  refresh: () => Promise<void>;
}

const ServerStatusContext = createContext<ServerStatusContextType | undefined>(undefined);

const REFRESH_INTERVAL = 30000; // 30 seconds

export function ServerStatusProvider({ children }: { children: ReactNode }) {
  const { config } = useConfig();
  const [servers, setServers] = useState<ServerInfo[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [refreshing, setRefreshing] = useState(false);

  const fetchServerStatus = async () => {
    try {
      setError(null);
      const api = createApiClient(config);
      const data = await api.get<ServerStatusResponse>("/api/v1/server-status");
      setServers(data.servers);
    } catch (err) {
      console.error("Failed to fetch server status:", err);
      if (err instanceof ApiError) {
        setError(err.message || "Failed to fetch server status");
      } else {
        setError(err instanceof Error ? err.message : "Failed to fetch server status");
      }
    } finally {
      setLoading(false);
      setRefreshing(false);
    }
  };

  const refresh = async () => {
    setRefreshing(true);
    await fetchServerStatus();
  };

  useEffect(() => {
    fetchServerStatus();

    // Set up auto-refresh every 30 seconds
    const intervalId = setInterval(() => {
      fetchServerStatus();
    }, REFRESH_INTERVAL);

    // Cleanup interval on unmount
    return () => clearInterval(intervalId);
  }, [config]);

  return (
    <ServerStatusContext.Provider value={{ servers, loading, error, refreshing, refresh }}>
      {children}
    </ServerStatusContext.Provider>
  );
}

export function useServerStatusData() {
  const context = useContext(ServerStatusContext);
  if (context === undefined) {
    throw new Error("useServerStatusData must be used within a ServerStatusProvider");
  }
  return context;
}

