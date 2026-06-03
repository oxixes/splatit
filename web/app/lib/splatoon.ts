import { createApiClient } from "~/lib/api-client";
import type { AppConfig } from "~/hooks/useAppConfig";
import { GAME_MODE_NAMES } from "~/constants/game-modes";
import type { ClientCountResponse, LobbiesResponse, LobbyCountResponse, FestivalTotalsResponse } from "~/types/splatoon";

export type {
  ClientCountResponse,
  FestivalTeamTotal,
  FestivalTotalsResponse,
  LobbiesResponse,
  Lobby,
  LobbyCountResponse,
} from "~/types/splatoon";

export async function getClientCount(config: AppConfig): Promise<ClientCountResponse> {
  const apiClient = createApiClient(config);
  return apiClient.get<ClientCountResponse>("/api/v1/splatoon/client_count");
}

export async function getLobbyCount(config: AppConfig): Promise<LobbyCountResponse> {
  const apiClient = createApiClient(config);
  return apiClient.get<LobbyCountResponse>("/api/v1/splatoon/lobby_count");
}

export async function getLobbies(config: AppConfig): Promise<LobbiesResponse> {
  const apiClient = createApiClient(config);
  return apiClient.get<LobbiesResponse>("/api/v1/splatoon/lobbies");
}

export async function getFestivalTotals(config: AppConfig, festivalId: number): Promise<FestivalTotalsResponse> {
  const apiClient = createApiClient(config);
  return apiClient.get<FestivalTotalsResponse>(`/api/v1/splatoon/festival_totals?festivalId=${festivalId}`);
}

export function getGameModeName(gameMode: number): string {
  return GAME_MODE_NAMES[gameMode] || `Mode ${gameMode}`;
}
