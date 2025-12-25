import { createApiClient } from "~/lib/api-client";
import type { AppConfig } from "~/hooks/useAppConfig";

export interface ServerStatus {
  version: number;
  status: "ok" | "down";
  error?: string;
}

export class ServerStatusError extends Error {
  constructor(
    message: string,
    public readonly type: "unreachable" | "incompatible" | "down",
    public readonly details?: string
  ) {
    super(message);
    this.name = "ServerStatusError";
  }
}

/**
 * Check the server status
 * @throws {ServerStatusError} If the server is unreachable, incompatible, or down
 */
export async function checkServerStatus(
  config: AppConfig
): Promise<ServerStatus> {
  const apiClient = createApiClient(config);

  try {
    const data = await apiClient.get<ServerStatus>('/status');

    // Check if server is down
    if (data.status === "down") {
      throw new ServerStatusError(
        "Server is currently down",
        "down",
        data.error || "No error message provided"
      );
    }

    // Check version compatibility
    if (config.compatibleVersions.length > 0 && !config.compatibleVersions.includes(data.version)) {
      throw new ServerStatusError(
        "Server version is not compatible",
        "incompatible",
        `Server version ${data.version} is not in the compatible versions list: ${config.compatibleVersions.join(", ")}`
      );
    }

    return data;
  } catch (error) {
    if (error instanceof ServerStatusError) {
      throw error;
    }

    // Network errors, timeout, API errors, etc.
    if (error instanceof Error) {
      throw new ServerStatusError(
        "Cannot reach the server",
        "unreachable",
        error.message
      );
    }

    throw new ServerStatusError(
      "Unknown error occurred",
      "unreachable",
      String(error)
    );
  }
}

