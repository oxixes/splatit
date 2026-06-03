import { createApiClient } from "~/lib/api-client";
import type { AppConfig } from "~/hooks/useAppConfig";

export interface SecurityStatus {
  allowAccountCreation: boolean;
  allowRealWiiU: boolean;
  allowGeneratedWiiU: boolean;
  maintenanceMode: boolean;
}

export async function getSecurityStatus(config: AppConfig): Promise<SecurityStatus> {
  const apiClient = createApiClient(config);
  return apiClient.get<SecurityStatus>("/api/v1/security-status");
}

export async function updateSecurityStatus(config: AppConfig, status: SecurityStatus): Promise<SecurityStatus> {
  const apiClient = createApiClient(config);
  return apiClient.put<SecurityStatus>("/api/v1/security-status", status);
}
