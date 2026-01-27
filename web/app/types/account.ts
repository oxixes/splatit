export interface Pagination {
  totalItems: number;
  totalPages: number;
  currentPage: number;
}

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

export interface DeviceAttribute {
  name: string;
  value: string;
  createdAt?: number;
}

export interface AccountOwnership {
  device: Device;
  status: number;
  lastUpdated?: number;
  attributes: DeviceAttribute[];
}

export interface AccountEmail {
  id?: number;
  address: string;
  parent: boolean;
  primary: boolean;
  reachable: boolean;
  type: string;
  updatedBy: string;
  validated: boolean;
  validatedAt?: number;
}

export interface AccountMii {
  id?: number;
  hash?: string;
  name: string;
  primary: boolean;
  data: string;
}

export interface AccountAgreement {
  type: string;
  version: number;
  country: string;
}

export interface Account {
  pid?: number;
  username: string;
  gender: number;
  region: number;
  timezone: string;
  language: string;
  active: boolean;
  country?: string;
  marketing?: boolean;
  offDevice?: boolean;
  birthdate?: string;
  created?: number;
  updated?: number;
  primaryEmail: AccountEmail;
  mii: AccountMii;
  signedAgreements: AccountAgreement[];
  ownedDevices: AccountOwnership[];
}

export interface AccountsResponse {
  accounts: Account[];
  pagination: Pagination;
}

export interface AccountResponse {
  account: Account;
}

export interface ListAccountsFilters {
  username?: string;
  gender?: string; // "male" | "female" or numeric string accepted by backend
  region?: number;
  active?: boolean;
  page?: number;
  pageSize?: number;
  sort?: string; // pid_asc, username_desc, etc.
}

export interface CreateAccountRequest {
  username: string;
  password: string;
  gender: string; // "male" | "female"
  region: number | string;
  timezone: string;
  language: string;
  country?: string;
  marketing?: boolean;
  offDevice?: boolean;
  birthdate?: string;
  email: {
    address: string;
    parent?: boolean;
    primary?: boolean;
    reachable?: boolean;
    type?: string;
    updatedBy?: string;
    validated?: boolean;
  };
  mii: {
    name: string;
    primary?: boolean;
    data: string;
  };
}

export interface UpdateAccountRequest {
  username?: string;
  gender?: string;
  region?: number | string;
  timezone?: string;
  language?: string;
  country?: string;
  marketing?: boolean;
  offDevice?: boolean;
  birthdate?: string;
  active?: boolean;
  email?: {
    address?: string;
    parent?: boolean;
    primary?: boolean;
    reachable?: boolean;
    type?: string;
    updatedBy?: string;
    validated?: boolean;
  };
  mii?: {
    name?: string;
    primary?: boolean;
    data?: string;
  };
}

export type DeviceStatusString = "active" | "inactive";

export interface LinkDeviceRequest {
  deviceId: number | string;
  status: DeviceStatusString;
  attributes?: Array<{ name: string; value: string }>;
}

export interface SetDeviceAttributeRequest {
  value: string;
}

export interface StatusOkResponse {
  status: "ok";
}
