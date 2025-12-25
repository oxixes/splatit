import { useState, useEffect } from "react";

export interface AppConfig {
  apiUrl: string;
  compatibleVersions: number[];
}

const defaultConfig: AppConfig = {
  apiUrl: "http://localhost:3000",
  compatibleVersions: [],
};

let cachedConfig: AppConfig | null = null;

export function useAppConfig() {
  const [config, setConfig] = useState<AppConfig>(cachedConfig || defaultConfig);
  const [loading, setLoading] = useState<boolean>(!cachedConfig);
  const [error, setError] = useState<Error | null>(null);

  useEffect(() => {
    if (cachedConfig) {
      return;
    }

    const loadConfig = async () => {
      try {
        const response = await fetch("/config.json");
        if (!response.ok) {
          throw new Error(`Failed to load config: ${response.statusText}`);
        }
        const data = await response.json();
        cachedConfig = data;
        setConfig(data);
      } catch (err) {
        console.error("Error loading config:", err);
        setError(err instanceof Error ? err : new Error("Unknown error"));
      } finally {
        setLoading(false);
      }
    };

    loadConfig();
  }, []);

  return { config, loading, error };
}

