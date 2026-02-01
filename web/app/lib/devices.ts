import { createApiClient } from "~/lib/api-client";
import type { AppConfig } from "~/hooks/useAppConfig";

export interface Device {
  id: number;
  language: string;
  platform: string;
  region: string;
  serialNumber: string;
  systemVersion: string;
  type: string;
  updatedBy: string;
  banned: boolean;
  status?: string;
  lastUpdated?: number;
}

export interface DevicesResponse {
  devices: Device[];
  pagination: {
    totalItems: number;
    totalPages: number;
    currentPage: number;
  };
}

export interface ListDevicesFilters {
  serialNumber?: string;
  platform?: string;
  region?: string;
  banned?: boolean;
  page?: number;
  pageSize?: number;
  sort?: string;
}

export interface CreateDeviceRequest {
  serialNumber: string;
  language: string;
  platform: string;
  region: string;
  systemVersion: string;
  type: string;
  banned?: boolean;
}

export interface DeviceResponse {
  device: Device;
}

export async function listDevices(config: AppConfig, filters?: ListDevicesFilters): Promise<DevicesResponse> {
  const apiClient = createApiClient(config);

  const params = new URLSearchParams();
  if (filters) {
    if (filters.serialNumber) params.append("serialNumber", filters.serialNumber);
    if (filters.platform !== undefined) params.append("platform", filters.platform.toString());
    if (filters.region !== undefined) params.append("region", filters.region.toString());
    if (filters.banned !== undefined) params.append("banned", filters.banned ? "true" : "false");
    if (filters.page !== undefined) params.append("page", filters.page.toString());
    if (filters.pageSize !== undefined) params.append("pageSize", filters.pageSize.toString());
    if (filters.sort) params.append("sort", filters.sort);
  }

  const url = `/api/v1/devices${params.toString() ? `?${params.toString()}` : ""}`;
  return apiClient.get<DevicesResponse>(url);
}

export async function getDevice(config: AppConfig, deviceId: number): Promise<DeviceResponse> {
  const apiClient = createApiClient(config);
  return apiClient.get<DeviceResponse>(`/api/v1/devices/${deviceId}`);
}

export async function createDevice(config: AppConfig, data: CreateDeviceRequest): Promise<DeviceResponse> {
  const apiClient = createApiClient(config);
  return apiClient.post<DeviceResponse>("/api/v1/devices", data);
}

export async function deleteDevice(config: AppConfig, deviceId: number): Promise<{ status: "ok" }> {
  const apiClient = createApiClient(config);
  return apiClient.delete<{ status: "ok" }>(`/api/v1/devices/${deviceId}`);
}

export async function updateDevice(
  config: AppConfig,
  deviceId: number,
  data: Partial<CreateDeviceRequest>
): Promise<DeviceResponse> {
  const apiClient = createApiClient(config);
  return apiClient.put<DeviceResponse>(`/api/v1/devices/${deviceId}`, data);
}

export async function banDevice(config: AppConfig, deviceId: number): Promise<DeviceResponse> {
  return updateDevice(config, deviceId, { banned: true });
}

export async function unbanDevice(config: AppConfig, deviceId: number): Promise<DeviceResponse> {
  return updateDevice(config, deviceId, { banned: false });
}

