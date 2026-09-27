import { notFound } from 'next/navigation';
import { ConsoleApp } from '@/components/console';

const validRoutes = [
  'overview',
  'pilot-summary',
  'book',
  'order-book',
  'execution',
  'telemetry',
  'operations',
  'system-health',
  'audit',
  'audit-trail',
  'sandbox',
  'api-console',
];

export default async function Page({
  params,
}: {
  params: Promise<{ screen: string }>;
}) {
  const { screen } = await params;
  if (!validRoutes.includes(screen)) {
    notFound();
  }
  return <ConsoleApp screen={screen} />;
}
