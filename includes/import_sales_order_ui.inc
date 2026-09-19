<?php
/**********************************************************************
    Copyright (C) FrontAccounting, LLC.
	Released under the terms of the GNU General Public License, GPL,
	as published by the Free Software Foundation, either version 3
	of the License, or (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the License here <http://www.gnu.org/licenses/gpl-3.0.html>.
***********************************************************************/

include_once(__DIR__ . "/import_sales_cart_class.inc");
include_once(__DIR__ . "/../../../manufacturing/includes/manufacturing_db.inc"); // FA 2.4: moved from includes/manufacturing.inc

//--------------------------------------------------------------------------------
/**
 * Adds an item, or every stock item of an item kit, to the document being imported.
 *
 * @return bool false when the item code is neither a stock item nor an item kit
 */
function import_add_to_order(Cart $order, string $new_item, float $new_item_qty, float $price, float $discount, string $description = ''): bool
{
	// calculate item price to sum of kit element prices factor for
	// value distribution over all exploded kit items
	$std_price = get_kit_price($new_item, $order->customer_currency,
		$order->sales_type, $order->price_factor, get_post('OrderDate'), true);

	if ($std_price == 0)
		$price_factor = 0.0;
	else
		$price_factor = $price/$std_price;

	$kit = get_item_kit($new_item);
	$item_num = db_num_rows($kit);
	if ($item_num == 0) {
		display_error(sprintf(_("Item code '%s' does not exist (no stock item or item kit has this code)."), $new_item));
		return false;
	}
	$added = true;
	while ($item = db_fetch($kit)) {
		$stock_id = (string)$item['stock_id'];
		$kit_quantity = (float)$item['quantity'];
		$std_price = get_kit_price($stock_id, $order->customer_currency,
			$order->sales_type, $order->price_factor, get_post('OrderDate'), true);

		// rounding differences are included in last price item in kit
		$item_num--;
		if ($item_num) {
			$price -= $kit_quantity*$std_price*$price_factor;
			$item_price = $std_price*$price_factor;
		} else {
			if ($kit_quantity != 0.0)
				$price = $price/$kit_quantity;
			$item_price = $price;
		}
		$item_price = round($item_price, user_price_dec());

		if ((int)$item['is_foreign'] == 0 && $item['item_code'] != $stock_id)
		{	// this is sales kit - recurse
			if (!import_add_to_order($order, $stock_id, $new_item_qty*$kit_quantity, $item_price, $discount)) {
				$added = false;
			}
		}
		else
		{	// stock item record eventually with foreign code

			// check duplicate stock item
			foreach ($order->line_items as $order_item)
			{
				if (strcasecmp($order_item->stock_id, $stock_id) == 0)
				{
					display_warning(_("For Part :").$stock_id. " "
						. _("This item is already on this document. You have been warned."));
					break;
				}
			}
			if ($order->add_to_cart(count($order->line_items), $stock_id,
				$new_item_qty*$kit_quantity, $item_price, $discount, 0, 0, $description) == 0) {
				$added = false; // FrontAccounting has already shown why
			}
		}
	}
	return $added;
}
