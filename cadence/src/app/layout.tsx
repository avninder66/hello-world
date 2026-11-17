import type { Metadata } from "next";
import { Geist, Geist_Mono } from "next/font/google";
import { PublicConfig } from "@/components/public-config";
import "./globals.css";

const geistSans = Geist({ variable: "--font-geist-sans", subsets: ["latin"] });
const geistMono = Geist_Mono({ variable: "--font-geist-mono", subsets: ["latin"] });

export const metadata: Metadata = {
  title: "Cadence",
  description: "Plan, approve and schedule social posts for every client — with Claude.",
};

export default function RootLayout({ children }: LayoutProps<"/">) {
  return (
    <html lang="en" className={`${geistSans.variable} ${geistMono.variable} h-full antialiased`}>
      <head>
        <PublicConfig />
      </head>
      <body className="min-h-full font-sans">{children}</body>
    </html>
  );
}
