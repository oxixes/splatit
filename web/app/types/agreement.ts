export interface Agreement {
  type: string;
  version: number;
  country: string;
  language: string;
  languageName: string;
  mainTitle: string;
  subTitle: string;
  mainContent: string;
  subContent: string;
  agreeButtonText: string;
  disagreeButtonText: string;
  publishDate?: number;
}

export interface AgreementsResponse {
  agreements: Agreement[];
  pagination: {
    totalItems: number;
    totalPages: number;
    currentPage: number;
  };
}

export interface DeleteAgreementRequest {
  type: string;
  version: number;
  country: string;
  language: string;
}

export type SortColumn = "type" | "country" | "language" | "version";
export type SortDirection = "asc" | "desc";
export type SortOption = `${SortColumn}_${SortDirection}`;

export interface AgreementsFilters {
  type?: string;
  country?: string;
  language?: string;
  version?: number;
  page?: number;
  pageSize?: number;
  sort?: SortOption;
}

