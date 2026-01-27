import { ArrowUp, ArrowDown, ChevronsUpDown } from "lucide-react";

export function SortableHeader({
  label,
  sortKey,
  currentSort,
  onSort,
}: {
  label: string;
  sortKey: string;
  currentSort?: string;
  onSort: (key: string) => void;
}) {
  const isSorted = currentSort?.startsWith(sortKey);
  const direction = isSorted ? (currentSort?.endsWith("_asc") ? "asc" : "desc") : null;

  return (
    <th
      className="text-left py-2 px-2 text-sm font-medium cursor-pointer hover:bg-muted/50 select-none"
      onClick={() => onSort(sortKey)}
    >
      <div className="flex items-center gap-1">
        <span>{label}</span>
        {direction === "asc" ? (
          <ArrowUp className="h-3 w-3" />
        ) : direction === "desc" ? (
          <ArrowDown className="h-3 w-3" />
        ) : (
          <ChevronsUpDown className="h-3 w-3 opacity-30" />
        )}
      </div>
    </th>
  );
}
