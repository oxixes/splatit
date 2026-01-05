import { createApiClient } from "~/lib/api-client";
import type { AppConfig } from "~/hooks/useAppConfig";
import type { Agreement, AgreementsResponse, DeleteAgreementRequest } from "~/types/agreement";

/**
 * Get all agreements from the server
 */
export async function getAgreements(config: AppConfig): Promise<Agreement[]> {
  const apiClient = createApiClient(config);

  try {
    const response = await apiClient.get<AgreementsResponse>("/api/v1/agreements");
    return response.agreements || [];
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

