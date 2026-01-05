export const AGREEMENT_TYPES = [
  { value: "NINTENDO-NETWORK-EULA", label: "Nintendo Network EULA" }
] as const;

export type AgreementTypeValue = typeof AGREEMENT_TYPES[number]["value"];

