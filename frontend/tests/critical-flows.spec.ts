'use client';

import { test, expect } from '@playwright/test';

test.describe('Corridor Frontend Critical Flows & Specifications', () => {
  test.beforeEach(async ({ page }) => {
    // Intercept backend API requests with accurate C++ contract mocks
    await page.route('**/api/corridor/config', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ orders: true, metrics: true, ledger: true }),
      });
    });

    await page.route('**/api/corridor/metrics', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'text/plain',
        body: [
          '# TYPE luv_execution_session_pnl gauge',
          'luv_execution_session_pnl 12345',
          '# TYPE luv_execution_gross_exposure gauge',
          'luv_execution_gross_exposure 987650000',
          '# TYPE luv_execution_fills_observed gauge',
          'luv_execution_fills_observed 14',
          '# TYPE luv_execution_fills_total counter',
          'luv_execution_fills_total 14',
          '# TYPE luv_execution_rejections_total counter',
          'luv_execution_rejections_total 2',
          '# TYPE luv_execution_tick_rate_hz gauge',
          'luv_execution_tick_rate_hz 48500',
          '# TYPE luv_execution_active_orders gauge',
          'luv_execution_active_orders 6',
          '# TYPE luv_execution_inference_latency_microseconds gauge',
          'luv_execution_inference_latency_microseconds 3.750',
          '# TYPE luv_execution_risk_check_latency_nanoseconds gauge',
          'luv_execution_risk_check_latency_nanoseconds 42.000',
          '# TYPE luv_telemetry_queue_depth gauge',
          'luv_telemetry_queue_depth 12',
          '# TYPE luv_telemetry_dropped_snapshots_total counter',
          'luv_telemetry_dropped_snapshots_total 0',
          '# TYPE luv_websocket_fill_notification_drops_total counter',
          'luv_websocket_fill_notification_drops_total 0',
          '# TYPE luv_execution_halted gauge',
          'luv_execution_halted 0',
        ].join('\n'),
      });
    });

    await page.route('**/api/corridor/health', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'text/plain',
        body: 'ok\n',
      });
    });

    await page.route('**/api/corridor/positions', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          positions: [
            { symbol_idx: 267, net_position: 100, gross_exposure: 1502500 },
          ],
        }),
      });
    });

    await page.route('**/api/corridor/ledger', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          total: 2,
          validation: 'valid',
          rows: [
            { sequence: '0', type: 'kAdd', order_id: '1001', quantity: '100', price: '1502500', side: 0, timestamp_ns: '1726000000000001000' },
            { sequence: '1', type: 'kFill', order_id: '1001', quantity: '100', price: '1502500', side: 0, timestamp_ns: '1726000000000002500' },
          ],
        }),
      });
    });
  });

  test('Persistent Sandbox Watermark is visible across screens', async ({ page }) => {
    await page.goto('/overview');
    const watermark = page.locator('[data-testid="sandbox-watermark-banner"]').first();
    await expect(watermark).toBeVisible();
    await expect(watermark).toContainText('SIMULATED DATA');
  });

  test('Flow 1: Submit limit order with optimistic pending -> backend confirmed live state', async ({ page }) => {
    await page.route('**/api/corridor/orders', async (route) => {
      if (route.request().method() === 'POST') {
        await route.fulfill({
          status: 202,
          contentType: 'application/json',
          body: JSON.stringify({ order_id: 101, status: 'accepted' }),
        });
      } else {
        await route.fallback();
      }
    });

    await page.goto('/execution');

    await page.fill('input[aria-label="Quantity"]', '50');
    await page.fill('input[aria-label="Limit Price"]', '150.25');

    const submitBtn = page.getByRole('button', { name: /Submit Order/i });
    await expect(submitBtn).toBeEnabled();
    await submitBtn.click();

    const orderRow = page.locator('table[aria-label="Orders Table"] tbody tr').first();
    await expect(orderRow).toBeVisible();
    await expect(orderRow).toContainText('#101');
    await expect(orderRow).toContainText(/live/i);
  });

  test('Flow 2: Cancel order flow with confirmation modal', async ({ page }) => {
    await page.route('**/api/corridor/orders', async (route) => {
      if (route.request().method() === 'POST') {
        await route.fulfill({
          status: 202,
          contentType: 'application/json',
          body: JSON.stringify({ order_id: 102, status: 'accepted' }),
        });
      }
    });

    await page.route('**/api/corridor/orders/102', async (route) => {
      if (route.request().method() === 'DELETE') {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({
            order_id: 102,
            status: 'cancelled',
            symbol_idx: 267,
            qty: 100,
            filled_qty: 0,
            price: 1502500,
          }),
        });
      }
    });

    await page.goto('/execution');
    await page.getByRole('button', { name: /Submit Order/i }).click();

    const cancelBtn = page.locator('table[aria-label="Orders Table"] button', { hasText: 'Cancel' }).first();
    await expect(cancelBtn).toBeVisible();
    await cancelBtn.click();

    const confirmBtn = page.locator('.dialog-content button', { hasText: /Cancel Order|Confirm/i }).last();
    await expect(confirmBtn).toBeVisible();
    await confirmBtn.click();

    const orderRow = page.locator('table[aria-label="Orders Table"] tbody tr').first();
    await expect(orderRow).toContainText(/cancelled/i);
  });

  test('Flow 3: Feed Stalled critical banner appears when feed is stalled', async ({ page }) => {
    await page.goto('/book');

    const toggleStallBtn = page.getByRole('button', { name: /Simulate Feed Stall|Toggle Feed Stall/i });
    await expect(toggleStallBtn).toBeVisible();
    await toggleStallBtn.click();

    const banner = page.locator('[data-testid="feed-stalled-banner"]');
    await expect(banner).toBeVisible();
    await expect(banner).toContainText('FEED STALLED');
  });

  test('Flow 4: Execution Halt critical banner appears when engine risk trips', async ({ page }) => {
    await page.route('**/api/corridor/metrics', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'text/plain',
        body: [
          '# TYPE luv_execution_halted gauge',
          'luv_execution_halted 1',
          '# TYPE luv_execution_tick_rate_hz gauge',
          'luv_execution_tick_rate_hz 0',
        ].join('\n'),
      });
    });

    await page.goto('/execution');

    const haltBanner = page.locator('[data-testid="execution-halt-banner"]');
    await expect(haltBanner).toBeVisible();
    await expect(haltBanner).toContainText('EXECUTION HALTED');

    const submitBtn = page.getByRole('button', { name: /Submit Order/i });
    await expect(submitBtn).toBeDisabled();
  });

  test('Flow 5 (Bug 1 Regression): When offline, Order Book and Audit Trail show EmptyState without fabricated data', async ({ page }) => {
    // Override routes with 503 Service Unavailable / Offline
    await page.route('**/api/corridor/metrics', async (route) => {
      await route.fulfill({ status: 503, body: 'Service Unavailable' });
    });
    await page.route('**/api/corridor/health', async (route) => {
      await route.fulfill({ status: 503, body: 'Service Unavailable' });
    });
    await page.route('**/api/corridor/ledger', async (route) => {
      await route.fulfill({ status: 503, body: JSON.stringify({ error: 'ledger_not_configured' }) });
    });

    // Check Order Book
    await page.goto('/book');
    await expect(page.locator('text=Awaiting Live LOB Depth Stream')).toBeVisible();

    // Check Audit Trail
    await page.goto('/audit');
    await expect(page.locator('text=Awaiting Engine Recovery Ledger Stream')).toBeVisible();
    await expect(page.locator('text=Awaiting Data')).toBeVisible();

    // Check Pilot Summary
    await page.goto('/overview');
    const firstMetric = page.locator('.panel-card span.tabular-nums').first();
    await expect(firstMetric).toHaveText('—');
  });

  test('Flow 6 (Part 2): Sandbox / API Console screen loads with dev tools', async ({ page }) => {
    await page.goto('/sandbox');

    const banner = page.locator('[data-testid="sandbox-banner"]');
    await expect(banner).toBeVisible();
    await expect(banner).toContainText('INTERNAL TOOL — NOT PART OF PILOT DEMO FLOW');

    // Check presence of REST panel and metrics button
    await expect(page.getByRole('button', { name: /Send Request/i })).toBeVisible();
    await expect(page.getByRole('button', { name: /Fetch \/metrics/i })).toBeVisible();
    await expect(page.getByRole('button', { name: /Connect Stream/i })).toBeVisible();
  });
});
