import { useMemo } from "react";
import { useConfig } from "~/contexts/AppConfigContext";
import { createApiClient } from "~/lib/api-client";
import type { ApiClient } from "~/lib/api-client";

/**
 * Hook to get an API client instance configured with the current app config
 *
 * @example
 * ```tsx
 * function MyComponent() {
 *   const api = useApiClient();
 *
 *   const fetchData = async () => {
 *     const data = await api.get('/api/players');
 *     console.log(data);
 *   };
 *
 *   return <button onClick={fetchData}>Fetch Data</button>;
 * }
 * ```
 */
export function useApiClient(): ApiClient {
  const { config } = useConfig();

  return useMemo(() => {
    return createApiClient(config);
  }, [config]);
}

