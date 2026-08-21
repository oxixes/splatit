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
    updatedBy: string;
    status: string;
    banned?: boolean;
}

export interface DeviceResponse {
    device: Device;
}