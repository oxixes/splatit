import {createApiClient} from "~/lib/api-client";
import type {AppConfig} from "~/hooks/useAppConfig";

export interface MapRotationResponse {
  rotation: {
    phases: PhaseData[];
  };
  afterFesBonusStart: string;
}

export interface PhaseData {
  gachiRule: string;
  regularRule: string;
  gachiStages: number[];
  regularStages: number[];
  duration: number;
}

export async function getMapRotation(config: AppConfig): Promise<MapRotationResponse> {
  const apiClient = createApiClient(config);
  return apiClient.get<MapRotationResponse>("/api/v1/map-rotation");
}

export async function updateMapRotation(config: AppConfig, rotation: { phases: PhaseData[] }): Promise<{ status: string }> {
  const apiClient = createApiClient(config);
  return apiClient.put<{ status: string }>("/api/v1/map-rotation", { rotation });
}

export async function randomizeMapRotation(config: AppConfig): Promise<{ status: string; rotation: { phases: PhaseData[] } }> {
  const apiClient = createApiClient(config);
  return apiClient.post<{ status: string; rotation: { phases: PhaseData[] } }>("/api/v1/map-rotation/randomize");
}
