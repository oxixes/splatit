import { createApiClient } from "~/lib/api-client";
import type { AppConfig } from "~/hooks/useAppConfig";
import type {
  AccountResponse,
  AccountsResponse,
  CreateAccountRequest,
  UpdateAccountRequest,
  LinkDeviceRequest,
  ListAccountsFilters,
  SetDeviceAttributeRequest,
  StatusOkResponse,
  DeviceAttribute,
} from "~/types/account";

export async function listAccounts(config: AppConfig, filters?: ListAccountsFilters): Promise<AccountsResponse> {
  const apiClient = createApiClient(config);

  const params = new URLSearchParams();
  if (filters) {
    if (filters.username) params.append("username", filters.username);
    if (filters.gender) params.append("gender", filters.gender);
    if (filters.region !== undefined) params.append("region", filters.region.toString());
    if (filters.active !== undefined) params.append("active", filters.active ? "true" : "false");
    if (filters.page !== undefined) params.append("page", filters.page.toString());
    if (filters.pageSize !== undefined) params.append("pageSize", filters.pageSize.toString());
    if (filters.sort) params.append("sort", filters.sort);
  }

  const url = `/api/v1/accounts${params.toString() ? `?${params.toString()}` : ""}`;
  return apiClient.get<AccountsResponse>(url);
}

export async function getAccount(config: AppConfig, pid: number): Promise<AccountResponse> {
  const apiClient = createApiClient(config);
  return apiClient.get<AccountResponse>(`/api/v1/accounts/${pid}`);
}

export async function createAccount(config: AppConfig, data: CreateAccountRequest): Promise<AccountResponse> {
  const apiClient = createApiClient(config);
  return apiClient.post<AccountResponse>("/api/v1/accounts", data);
}

export async function updateAccount(config: AppConfig, pid: number, data: UpdateAccountRequest): Promise<AccountResponse> {
  const apiClient = createApiClient(config);
  return apiClient.put<AccountResponse>(`/api/v1/accounts/${pid}`, data);
}

export async function deleteAccount(config: AppConfig, pid: number): Promise<StatusOkResponse> {
  const apiClient = createApiClient(config);
  return apiClient.delete<StatusOkResponse>(`/api/v1/accounts/${pid}`);
}

export async function linkDeviceToAccount(config: AppConfig, pid: number, data: LinkDeviceRequest): Promise<StatusOkResponse> {
  const apiClient = createApiClient(config);
  return apiClient.post<StatusOkResponse>(`/api/v1/accounts/${pid}/devices`, data);
}

export async function unlinkDeviceFromAccount(config: AppConfig, pid: number, deviceId: number): Promise<StatusOkResponse> {
  const apiClient = createApiClient(config);
  return apiClient.delete<StatusOkResponse>(`/api/v1/accounts/${pid}/devices/${deviceId}`);
}

export async function updateAccountDeviceStatus(
  config: AppConfig,
  pid: number,
  deviceId: number,
  status: "active" | "inactive",
): Promise<StatusOkResponse> {
  const apiClient = createApiClient(config);
  return apiClient.put<StatusOkResponse>(`/api/v1/accounts/${pid}/devices/${deviceId}/status`, { status });
}

export async function listAccountDeviceAttributes(
  config: AppConfig,
  pid: number,
  deviceId: number,
): Promise<{ attributes: DeviceAttribute[] }> {
  const apiClient = createApiClient(config);
  return apiClient.get<{ attributes: DeviceAttribute[] }>(`/api/v1/accounts/${pid}/devices/${deviceId}/attributes`);
}

export async function setAccountDeviceAttribute(
  config: AppConfig,
  pid: number,
  deviceId: number,
  attributeName: string,
  data: SetDeviceAttributeRequest,
): Promise<StatusOkResponse> {
  const apiClient = createApiClient(config);
  return apiClient.put<StatusOkResponse>(
    `/api/v1/accounts/${pid}/devices/${deviceId}/attributes/${encodeURIComponent(attributeName)}`,
    data,
  );
}

export async function removeAccountDeviceAttribute(
  config: AppConfig,
  pid: number,
  deviceId: number,
  attributeName: string,
): Promise<StatusOkResponse> {
  const apiClient = createApiClient(config);
  return apiClient.delete<StatusOkResponse>(
    `/api/v1/accounts/${pid}/devices/${deviceId}/attributes/${encodeURIComponent(attributeName)}`,
  );
}

export interface CemuFilesResponse {
  accountDat: string;
  otp: string;
  seeprom: string;
  clientCert: string;
  clientKey: string;
  serverCert: string;
  networkServices: string;
  persistentId: string;
}

export async function getCemuFiles(
  config: AppConfig,
  pid: number,
  password: string,
): Promise<CemuFilesResponse> {
  const apiClient = createApiClient(config);
  return apiClient.post<CemuFilesResponse>(`/api/v1/accounts/${pid}/cemu-files`, { password });
}

