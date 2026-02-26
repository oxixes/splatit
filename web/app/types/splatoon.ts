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