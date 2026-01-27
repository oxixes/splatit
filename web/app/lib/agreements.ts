import {createApiClient} from "~/lib/api-client";
import type {AppConfig} from "~/hooks/useAppConfig";
import type {Agreement, AgreementsFilters, AgreementsResponse, DeleteAgreementRequest} from "~/types/agreement";

/**
 * Get all agreements from the server with optional filtering, pagination and sorting
 */
export async function getAgreements(config: AppConfig, filters?: AgreementsFilters): Promise<AgreementsResponse> {
  const apiClient = createApiClient(config);

  try {
    const params = new URLSearchParams();

    if (filters) {
      if (filters.type) params.append("type", filters.type);
      if (filters.country) params.append("country", filters.country);
      if (filters.language) params.append("language", filters.language);
      if (filters.version !== undefined) params.append("version", filters.version.toString());
      if (filters.page !== undefined) params.append("page", filters.page.toString());
      if (filters.pageSize !== undefined) params.append("pageSize", filters.pageSize.toString());
      if (filters.sort) params.append("sort", filters.sort);
    }

    const url = `/api/v1/agreements${params.toString() ? `?${params.toString()}` : ""}`;
    return await apiClient.get<AgreementsResponse>(url);
  } catch (error) {
    console.error("Error loading agreements:", error);
    throw error;
  }
}

/**
 * Create or update an agreement on the server
 */
export async function saveAgreement(config: AppConfig, agreement: Agreement): Promise<void> {
  const apiClient = createApiClient(config);

  try {
    await apiClient.post<void>("/api/v1/agreements", agreement);
  } catch (error) {
    console.error("Error saving agreement:", error);
    throw error;
  }
}

/**
 * Delete an agreement from the server
 */
export async function deleteAgreement(config: AppConfig, request: DeleteAgreementRequest): Promise<void> {
  const apiClient = createApiClient(config);

  try {
    await apiClient.deleteWithBody<void>("/api/v1/agreements", request);
  } catch (error) {
    console.error("Error deleting agreement:", error);
    throw error;
  }
}

