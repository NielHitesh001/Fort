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
  });

  test('Persistent Sandbox Watermark is visible across screens', async ({ page }) => {
    await page.goto('/overview');
    const watermark = page.locator('[data-testid="sandbox-watermark-banner"]').first();
    await expect(watermark).toBeVisible();
    await expect(watermark).toContainText('SIMULATED DATA');
  });

  test('Flow 1: Submit limit order with optimistic pending -> backend confirmed live state', async ({ page }) => {
    // Intercept POST /api/corridor/orders to return 202 Accepted
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

    // Fill order ticket
    await page.fill('input[aria-label="Quantity"]', '50');
    await page.fill('input[aria-label="Limit Price"]', '150.25');

    // Submit order
    const submitBtn = page.getByRole('button', { name: /Submit Order/i });
    await expect(submitBtn).toBeEnabled();
    await submitBtn.click();

    // Verify order appears in table with live status
    const orderRow = page.locator('table[aria-label="Orders Table"] tbody tr').first();
    await expect(orderRow).toBeVisible();
    await expect(orderRow).toContainText('#101');
    await expect(orderRow).toContainText(/live/i);
  });

  test('Flow 2: Cancel order flow with confirmation modal', async ({ page }) => {
    // Seed an order
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

    // Find and click Cancel button in table
    const cancelBtn = page.locator('table[aria-label="Orders Table"] button', { hasText: 'Cancel' }).first();
    await expect(cancelBtn).toBeVisible();
    await cancelBtn.click();

    // Modal appears and confirmation is clicked
    const confirmBtn = page.locator('.dialog-content button', { hasText: /Cancel Order|Confirm/i }).last();
    await expect(confirmBtn).toBeVisible();
    await confirmBtn.click();

    // Status updates to cancelled
    const orderRow = page.locator('table[aria-label="Orders Table"] tbody tr').first();
    await expect(orderRow).toContainText(/cancelled/i);
  });

  test('Flow 3: Feed Stalled critical banner appears when feed is stalled', async ({ page }) => {
    await page.goto('/book');

    // Toggle Feed Stall simulation button on screen
    const toggleStallBtn = page.getByRole('button', { name: /Toggle Feed Stall/i });
    await expect(toggleStallBtn).toBeVisible();
    await toggleStallBtn.click();

    // Stalled critical banner appears
    const banner = page.locator('[data-testid="feed-stalled-banner"]');
    await expect(banner).toBeVisible();
    await expect(banner).toContainText('FEED STALLED');
  });

  test('Flow 4: Execution Halt critical banner appears when engine risk trips', async ({ page }) => {
    // Override metrics with halted = 1
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

    // Execution halt banner should appear
    const haltBanner = page.locator('[data-testid="execution-halt-banner"]');
    await expect(haltBanner).toBeVisible();
    await expect(haltBanner).toContainText('EXECUTION HALTED');

    // Order submit button should be disabled
    const submitBtn = page.getByRole('button', { name: /Submit Order/i });
    await expect(submitBtn).toBeDisabled();
  });
});
