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
}

export interface DeleteAgreementRequest {
  type: string;
  version: number;
  country: string;
  language: string;
}

