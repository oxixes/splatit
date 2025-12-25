import { createContext, useContext } from "react";
import type { ReactNode } from "react";
import { useAppConfig } from "~/hooks/useAppConfig";
import type { AppConfig } from "~/hooks/useAppConfig";

interface AppConfigContextType {
  config: AppConfig;
  loading: boolean;
  error: Error | null;
}

const AppConfigContext = createContext<AppConfigContextType | undefined>(undefined);

export function AppConfigProvider({ children }: { children: ReactNode }) {
  const { config, loading, error } = useAppConfig();

  return (
    <AppConfigContext.Provider value={{ config, loading, error }}>
      {children}
    </AppConfigContext.Provider>
  );
}

export function useConfig() {
  const context = useContext(AppConfigContext);
  if (context === undefined) {
    throw new Error("useConfig must be used within an AppConfigProvider");
  }
  return context;
}

