import {createApiClient} from "~/lib/api-client";
import type {AppConfig} from "~/hooks/useAppConfig";

export interface FestivalListResponse {
  festivals: unknown[];
  activeId: number;
}

export interface ActiveFestivalResponse {
  activeId: number;
  festival: unknown;
}

export async function getFestivals(config: AppConfig): Promise<FestivalListResponse> {
  const apiClient = createApiClient(config);
  return apiClient.get<FestivalListResponse>("/api/v1/festivals");
}

export async function getActiveFestival(config: AppConfig): Promise<ActiveFestivalResponse> {
  const apiClient = createApiClient(config);
  return apiClient.get<ActiveFestivalResponse>("/api/v1/festivals/active");
}

export async function saveFestival(config: AppConfig, festival: unknown): Promise<{ status: string; id: number }> {
  const apiClient = createApiClient(config);
  return apiClient.post<{ status: string; id: number }>("/api/v1/festivals", festival);
}

export async function deleteFestival(config: AppConfig, id: number): Promise<{ status: string }> {
  const apiClient = createApiClient(config);
  return apiClient.delete<{ status: string }>(`/api/v1/festivals/${id}`);
}

export async function switchActiveFestival(config: AppConfig, id: number): Promise<{ status: string; activeId: number }> {
  const apiClient = createApiClient(config);
  return apiClient.post<{ status: string; activeId: number }>("/api/v1/festivals/switch", { id });
}
