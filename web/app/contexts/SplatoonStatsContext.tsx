import { createContext, useContext, useState, useEffect } from "react";
import type { ReactNode } from "react";
import { useConfig } from "~/contexts/AppConfigContext";
import { getClientCount, getLobbyCount, getLobbies } from "~/lib/splatoon";
import type { Lobby } from "~/lib/splatoon";
import { ApiError } from "~/lib/api-client";

interface SplatoonStatsContextType {
  clientCount: number;
  lobbyCount: number;
  lobbies: Lobby[];
  loading: boolean;
  error: string | null;
  refreshing: boolean;
  refresh: () => Promise<void>;
}

const SplatoonStatsContext = createContext<SplatoonStatsContextType | undefined>(undefined);

const REFRESH_INTERVAL = 30000; // 30 seconds

export function SplatoonStatsProvider({ children }: { children: ReactNode }) {
  const { config } = useConfig();
  const [clientCount, setClientCount] = useState(0);
  const [lobbyCount, setLobbyCount] = useState(0);
  const [lobbies, setLobbies] = useState<Lobby[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [refreshing, setRefreshing] = useState(false);

  const fetchSplatoonStats = async () => {
    try {
      setError(null);
      const [clientCountRes, lobbyCountRes, lobbiesRes] = await Promise.all([
        getClientCount(config),
        getLobbyCount(config),
        getLobbies(config),
      ]);
      setClientCount(clientCountRes.count);
      setLobbyCount(lobbyCountRes.count);
      setLobbies(lobbiesRes.lobbies);
    } catch (err) {
      console.error("Failed to fetch Splatoon stats:", err);
      if (err instanceof ApiError) {
        setError(err.message || "Failed to fetch Splatoon stats");
      } else {
        setError(err instanceof Error ? err.message : "Failed to fetch Splatoon stats");
      }
    } finally {
      setLoading(false);
      setRefreshing(false);
    }
  };

  const refresh = async () => {
    setRefreshing(true);
    await fetchSplatoonStats();
  };

  useEffect(() => {
    fetchSplatoonStats();

    // Set up auto-refresh every 30 seconds
    const intervalId = setInterval(() => {
      fetchSplatoonStats();
    }, REFRESH_INTERVAL);

    // Cleanup interval on unmount
    return () => clearInterval(intervalId);
  }, [config]);

  return (
    <SplatoonStatsContext.Provider value={{ clientCount, lobbyCount, lobbies, loading, error, refreshing, refresh }}>
      {children}
    </SplatoonStatsContext.Provider>
  );
}

export function useSplatoonStats() {
  const context = useContext(SplatoonStatsContext);
  if (context === undefined) {
    throw new Error("useSplatoonStats must be used within a SplatoonStatsProvider");
  }
  return context;
}

