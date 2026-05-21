export interface FestivalListResponse {
    festivals: FestivalSummary[];
    activeId: number;
}

export interface FestivalSummary {
    id: number;
    teamAName: string;
    teamBName: string;
    active: boolean;
}

export interface ActiveFestivalResponse {
    activeId: number;
    festival: unknown;
}
