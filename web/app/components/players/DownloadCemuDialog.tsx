import { useState } from "react";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "~/components/ui/dialog";
import { Button } from "~/components/ui/button";
import { Input } from "~/components/ui/input";
import { Label } from "~/components/ui/label";
import type { AppConfig } from "~/hooks/useAppConfig";
import { getCemuFiles } from "~/lib/accounts";
import { ApiError, ManagementError } from "~/lib/api-client";
import JSZip from "jszip";
import { Download, Loader2 } from "lucide-react";

interface DownloadCemuDialogProps {
  config: AppConfig;
  pid: number | null;
  username: string | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
}

// List of server certificate filenames for scerts folder
const SERVER_CERT_FILES = [
  "ADDTRUST_EXT_CA_ROOT.der",
  "AMAZON_ROOT_CA1.der",
  "BALTIMORE_CYBERTRUST_ROOT_CA.der",
  "CACERT_NINTENDO_CA.der",
  "CACERT_NINTENDO_CA_G2.der",
  "CACERT_NINTENDO_CA_G3.der",
  "CACERT_NINTENDO_CLASS2_CA.der",
  "CACERT_NINTENDO_CLASS2_CA_G2.der",
  "CACERT_NINTENDO_CLASS2_CA_G3.der",
  "COMODO_CA.der",
  "COMODO_RSA_CA.der",
  "CYBERTRUST_GLOBAL_ROOT_CA.der",
  "DIGICERT_ASSURED_ID_ROOT_CA.der",
  "DIGICERT_ASSURED_ID_ROOT_CA_G2.der",
  "DIGICERT_GLOBAL_ROOT_CA.der",
  "DIGICERT_GLOBAL_ROOT_CA_G2.der",
  "DIGICERT_HIGH_ASSURANCE_EV_ROOT_CA.der",
  "ENTRUST_CA_2048.der",
  "ENTRUST_ROOT_CA.der",
  "ENTRUST_ROOT_CA_G2.der",
  "ENTRUST_SECURE_SERVER_CA.der",
  "EQUIFAX_SECURE_CA.der",
  "GEOTRUST_GLOBAL_CA.der",
  "GEOTRUST_GLOBAL_CA2.der",
  "GEOTRUST_PRIMARY_CA.der",
  "GEOTRUST_PRIMARY_CA_G3.der",
  "GLOBALSIGN_ROOT_CA.der",
  "GLOBALSIGN_ROOT_CA_R2.der",
  "GLOBALSIGN_ROOT_CA_R3.der",
  "GTE_CYBERTRUST_GLOBAL_ROOT.der",
  "NTD_DEV_CA.der",
  "STARFIELD_SERVICES_ROOT_CERTIFICATE_AUTHORITY_G2.der",
  "THAWTE_PREMIUM_SERVER_CA.der",
  "THAWTE_PRIMARY_ROOT_CA.der",
  "THAWTE_PRIMARY_ROOT_CA_G3.der",
  "USERTRUST_RSA_CA.der",
  "UTN_DATACORP_SGC_CA.der",
  "UTN_USERFIRST_HARDWARE_CA.der",
  "VERISIGN_CLASS3_PUBLIC_PRIMARY_CA.der",
  "VERISIGN_CLASS3_PUBLIC_PRIMARY_CA_G2.der",
  "VERISIGN_CLASS3_PUBLIC_PRIMARY_CA_G3.der",
  "VERISIGN_CLASS3_PUBLIC_PRIMARY_CA_G5.der",
  "VERISIGN_UNIVERSAL_ROOT_CA.der",
  "VERIZON_GLOBAL_ROOT_CA.der",
];

function base64ToUint8Array(base64: string): Uint8Array {
  const binaryString = atob(base64);
  const len = binaryString.length;
  const bytes = new Uint8Array(len);
  for (let i = 0; i < len; i++) {
    bytes[i] = binaryString.charCodeAt(i);
  }
  return bytes;
}

export function DownloadCemuDialog({ config, pid, username, open, onOpenChange }: DownloadCemuDialogProps) {
  const [password, setPassword] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const handleDownload = async () => {
    if (!pid) return;
    if (!password.trim()) {
      setError("Password is required");
      return;
    }

    setLoading(true);
    setError(null);

    try {
      const response = await getCemuFiles(config, pid, password);

      // Create ZIP file
      const zip = new JSZip();

      // Decode base64 files
      const accountDat = base64ToUint8Array(response.accountDat);
      const otp = base64ToUint8Array(response.otp);
      const seeprom = base64ToUint8Array(response.seeprom);
      const clientCert = base64ToUint8Array(response.clientCert);
      const clientKey = base64ToUint8Array(response.clientKey);
      const serverCert = base64ToUint8Array(response.serverCert);
      const networkServices = base64ToUint8Array(response.networkServices);

      // Root files
      zip.file("otp.bin", otp);
      zip.file("seeprom.bin", seeprom);
      zip.file("network_services.xml", networkServices);

      // Account.dat
      zip.file(`mlc01/usr/save/system/act/${response.persistentId}/account.dat`, accountDat);

      // Client certificates (all 5 are the same)
      const certNames = [
        "WIIU_ACCOUNT_1_CERT.der",
        "WIIU_COMMON_1_CERT.der",
        "WIIU_OLIVE_1_CERT.der",
        "WIIU_VINO_1_CERT.der",
        "WIIU_WOOD_1_CERT.der",
      ];
      for (const certName of certNames) {
        zip.file(`mlc01/sys/title/0005001b/10054000/content/ccerts/${certName}`, clientCert);
      }

      // Client keys (all 5 are the same)
      const keyNames = [
        "WIIU_ACCOUNT_1_RSA_KEY.aes",
        "WIIU_COMMON_1_RSA_KEY.aes",
        "WIIU_OLIVE_1_RSA_KEY.aes",
        "WIIU_VINO_1_RSA_KEY.aes",
        "WIIU_WOOD_1_RSA_KEY.aes",
      ];
      for (const keyName of keyNames) {
        zip.file(`mlc01/sys/title/0005001b/10054000/content/ccerts/${keyName}`, clientKey);
      }

      // Server certificates (all are copies of serverCert)
      for (const scertName of SERVER_CERT_FILES) {
        zip.file(`mlc01/sys/title/0005001b/10054000/content/scerts/${scertName}`, serverCert);
      }

      // Generate ZIP file
      const blob = await zip.generateAsync({ type: "blob" });

      // Download the file
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = `cemu_files_${username || pid}.zip`;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);

      // Close dialog and reset
      setPassword("");
      setError(null);
      onOpenChange(false);
    } catch (err) {
      if (err instanceof ApiError) {
        // Handle specific API error codes from the server
        switch (err.code) {
          case ManagementError.NOT_FOUND:
            setError("Account or device not found. Please ensure you have a linked device.");
            break;
          case ManagementError.PERMISSION_DENIED:
            setError("Invalid password. Please check your password and try again.");
            break;
          case ManagementError.INTERNAL_ERROR:
            setError(err.message || "Internal server error. Contact administrator if this persists.");
            break;
          case ManagementError.BAD_GATEWAY:
            setError(err.message || `Failed to get CEMU files`);
            break;
          case ManagementError.BAD_REQUEST:
            setError("Bad request. Please check your input.");
            break;
          case ManagementError.METHOD_NOT_ALLOWED:
            setError("Method not allowed. Please contact administrator.");
            break;
          default:
            setError(err.message || "An unexpected error occurred. Please try again.");
        }
      } else if (err instanceof Error) {
        setError(err.message || "An unexpected error occurred. Please try again.");
      } else {
        setError("An unexpected error occurred. Please try again.");
      }
    } finally {
      setLoading(false);
    }
  };

  const handleClose = (open: boolean) => {
    if (!loading) {
      setPassword("");
      setError(null);
      onOpenChange(open);
    }
  };

  return (
    <Dialog open={open} onOpenChange={handleClose}>
      <DialogContent className="sm:max-w-md">
        <DialogHeader>
          <DialogTitle>Download CEMU Files</DialogTitle>
          <DialogDescription>
            Enter the account password to download CEMU emulator files for {username || `PID ${pid}`}.
          </DialogDescription>
        </DialogHeader>

        <div className="space-y-4 py-4">
          <div className="space-y-2">
            <Label htmlFor="password">Password</Label>
            <Input
              id="password"
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter" && !loading) {
                  void handleDownload();
                }
              }}
              placeholder="Enter account password"
              disabled={loading}
            />
          </div>

          {error && <div className="text-sm text-destructive">{error}</div>}
        </div>

        <DialogFooter>
          <Button variant="outline" onClick={() => handleClose(false)} disabled={loading}>
            Cancel
          </Button>
          <Button onClick={() => void handleDownload()} disabled={loading || !password.trim()}>
            {loading && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
            {loading ? "Downloading..." : <><Download className="h-4 w-4" />Download</>}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
