import type { AppConfig } from "~/hooks/useAppConfig";

/**
 * Management API error codes
 */
export enum ManagementError {
  SUCCESS = 0,
  BAD_REQUEST = 4000,
  PERMISSION_DENIED = 4010,
  NOT_FOUND = 4040,
  METHOD_NOT_ALLOWED = 4050,
  CONFLICT = 4090,
  INTERNAL_ERROR = 5000,
  BAD_GATEWAY = 5020,
}

/**
 * Error response from the API
 */
export interface ApiErrorResponse {
  error: {
    code: ManagementError;
    message: string;
  };
}

/**
 * Custom error class for API errors
 */
export class ApiError extends Error {
  public code: ManagementError;
  public httpStatus: number;

  constructor(code: ManagementError, message: string, httpStatus: number) {
    super(message);
    this.name = "ApiError";
    this.code = code;
    this.httpStatus = httpStatus;
  }
}

/**
 * API client for making requests to the Management UI server
 */
export class ApiClient {
  private baseUrl: string;

  constructor(config: AppConfig) {
    this.baseUrl = config.apiUrl;
  }

  /**
   * Make a GET request to the API
   */
  async get<T>(endpoint: string): Promise<T> {
    const response = await fetch(`${this.baseUrl}${endpoint}`, {
      method: "GET",
      headers: {
        "Content-Type": "application/json",
      },
    });

    if (!response.ok) {
      await this.handleErrorResponse(response);
    }

    return response.json();
  }

  /**
   * Make a POST request to the API
   */
  async post<T>(endpoint: string, data?: unknown): Promise<T> {
    const response = await fetch(`${this.baseUrl}${endpoint}`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: data ? JSON.stringify(data) : undefined,
    });

    if (!response.ok) {
      await this.handleErrorResponse(response);
    }

    return response.json();
  }

  /**
   * Make a PUT request to the API
   */
  async put<T>(endpoint: string, data?: unknown): Promise<T> {
    const response = await fetch(`${this.baseUrl}${endpoint}`, {
      method: "PUT",
      headers: {
        "Content-Type": "application/json",
      },
      body: data ? JSON.stringify(data) : undefined,
    });

    if (!response.ok) {
      await this.handleErrorResponse(response);
    }

    return response.json();
  }

  /**
   * Make a DELETE request to the API
   */
  async delete<T>(endpoint: string): Promise<T> {
    const response = await fetch(`${this.baseUrl}${endpoint}`, {
      method: "DELETE",
      headers: {
        "Content-Type": "application/json",
      },
    });

    if (!response.ok) {
      await this.handleErrorResponse(response);
    }

    return response.json();
  }

  /**
   * Make a DELETE request with a body to the API
   */
  async deleteWithBody<T>(endpoint: string, data: unknown): Promise<T> {
    const response = await fetch(`${this.baseUrl}${endpoint}`, {
      method: "DELETE",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify(data),
    });

    if (!response.ok) {
      await this.handleErrorResponse(response);
    }

    return response.json();
  }

  /**
   * Handle error response from API
   */
  private async handleErrorResponse(response: Response): Promise<never> {
    try {
      const errorData = await response.json() as ApiErrorResponse;
      throw new ApiError(errorData.error.code, errorData.error.message, response.status);
    } catch (error) {
      // If JSON parsing fails, throw generic error
      if (error instanceof ApiError) {
        throw error;
      }
      throw new ApiError(ManagementError.INTERNAL_ERROR, `API request failed: ${response.statusText}`, response.status);
    }
  }
}

/**
 * Create an API client instance from the app configuration
 */
export function createApiClient(config: AppConfig): ApiClient {
  return new ApiClient(config);
}

