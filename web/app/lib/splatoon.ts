import { createApiClient } from "~/lib/api-client";
import type { AppConfig } from "~/hooks/useAppConfig";

export interface ClientCountResponse {
  count: number;
}

export interface LobbyCountResponse {
  count: number;
}

export interface Lobby {
  attributes: number[];
  description: string;
  flags: number;
  gId: number;
  gameMode: number;
  hostPid: number;
  matchmakeSystemType: number;
  maxParticipants: number;
  minParticipants: number;
  openParticipation: boolean;
  option0: number;
  ownerPid: number;
  participationPolicy: number;
  playerPids: number[];
  policyArgument: number;
  startedTime: number;
  state: number;
  systemPasswordEnabled: boolean;
  userPasswordEnabled: boolean;
}

export interface LobbiesResponse {
  lobbies: Lobby[];
}

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

// Game mode names mapping
export const GAME_MODE_NAMES: Record<number, string> = {
  2: "Team matchmaking",
  3: "Private battle",
};

export function getGameModeName(gameMode: number): string {
  return GAME_MODE_NAMES[gameMode] || `Mode ${gameMode}`;
}

