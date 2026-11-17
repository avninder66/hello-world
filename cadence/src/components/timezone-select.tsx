"use client";

import { useMemo } from "react";

export function TimezoneSelect({ id, name, defaultValue }: { id: string; name: string; defaultValue?: string }) {
  const zones = useMemo(() => Intl.supportedValuesOf("timeZone"), []);
  const fallback = defaultValue ?? "Europe/London";
  return (
    <select id={id} name={name} defaultValue={fallback} className="input">
      {zones.map((z) => <option key={z} value={z}>{z.replaceAll("_", " ")}</option>)}
    </select>
  );
}
