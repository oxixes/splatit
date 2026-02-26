export interface ServerStatus {
    version: number;
    status: "ok" | "down";
    error?: string;
}