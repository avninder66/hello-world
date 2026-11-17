import { requireUser } from "@/lib/auth";
import { NewWorkspaceForm } from "./form";

export default async function NewWorkspacePage() {
  await requireUser();
  return (
    <main className="mx-auto max-w-md px-4 py-12">
      <h1 className="h1 mb-1">New workspace</h1>
      <p className="mb-6 text-sm text-muted">One workspace per client or brand. Each has its own accounts, calendar, team and approval rules.</p>
      <NewWorkspaceForm />
    </main>
  );
}
