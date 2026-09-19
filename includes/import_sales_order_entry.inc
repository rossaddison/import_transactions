<?php

include_once(__DIR__ . "/import_rows.inc");
include_once(__DIR__ . "/import_sales_cart_class.inc"); // the cart that carries one imported document
include_once(__DIR__ . "/../../../includes/session.inc");
include_once(__DIR__ . "/../../../sales/includes/sales_ui.inc");
include_once(__DIR__ . "/../../../sales/includes/ui/sales_order_ui.inc");
include_once(__DIR__ . "/../../../sales/includes/sales_db.inc");
include_once(__DIR__ . "/../../../sales/includes/db/sales_types_db.inc");
include_once(__DIR__ . "/../../../reporting/includes/reporting.inc");

/**
 * @psalm-mutation-free
 * @param string $key column of the cart's payment terms record, e.g. 'cash_sale'
 */
function import_payment_terms_flag(Cart $cart, string $key): bool
{
	return isset($cart->payment_terms[$key]) && (int)$cart->payment_terms[$key] != 0;
}

/**
 * Validates one csv line of a sales order / invoice against the document being built.
 *
 * $line is the csv line number, used in the error messages only.
 */
function can_process(int $line, import_sales_line $row, Cart $cart): bool
{
	/** @var sys_prefs $SysPrefs */
	global $SysPrefs;

	if ($row->customer_id === '')
	{
		display_error(_("On line $line there is no customer input."));
		return false;
	}

	if ($row->branch_no === '')
	{
		display_error(_("On line $line this customer has no branch defined."));
		return false;
	}

	if (!customer_branch_exists($row->customer_id, $row->branch_no))
	{
		display_error(_("On line $line this branch number does not exist for this customer."));
		return false;
	}

	if (trim($row->reference) === "") {
		display_error(_("On line $line you must enter a reference."));
		return false;
	}
	if ($row->date === '') {
		display_error(_("On line $line you must enter a valid date."));
		return false;
	}
	if (get_sales_type_id($row->sales_type_name) == 0) {
		display_error(_("On line $line the sales type '{$row->sales_type_name}' does not exist."));
		return false;
	}
	if ($cart->trans_type != ST_SALESORDER && $cart->trans_type != ST_SALESQUOTE && !is_date_in_fiscalyear($row->date)) {
		display_error(_("On line $line the entered date is not in the fiscal year"));
		return false;
	}
	if (!$SysPrefs->allow_negative_stock() && count($cart->check_qoh()) > 0)
	{
		display_error(_("Line $line cannot be processed because there is insufficient quantity for items marked."));
		return false;
	}
	if (!import_payment_terms_flag($cart, 'cash_sale')) {

		if (strlen($row->delto) <= 1) {
			display_error(_("On line $line you must enter the person or company to whom delivery should be made to."));
			return false;
		}

		if ($cart->trans_type != ST_SALESQUOTE && strlen($row->deladdress) <= 1) {
			display_error( _("On line $line you should enter the street address in the box provided. Orders cannot be accepted without a valid street address."));
			return false;
		}

		// The freight column of the csv is not imported: every imported document has freight of 0.

		if (!is_date($row->deldate)) {
			if ($cart->trans_type == ST_SALESQUOTE)
				display_error(_("On line $line the valid date is invalid."));
			else
				display_error(_("On line $line the delivery date is invalid."));
			return false;
		}

		if (date1_greater_date2($row->date, $row->deldate)) {
			if ($cart->trans_type == ST_SALESQUOTE)
				display_error(_("On line $line the requested valid date is before the date of the quotation."));
			else
				display_error(_("On line $line the requested delivery date is before the date of the order."));
			return false;
		}
	}
	else
	{
		if (!db_has_cash_accounts())
		{
			display_error(_("On line $line you need to define a cash account for your Sales Point."));
			return false;
		}
	}

	if (!db_has_currency_rates($cart->customer_currency, $row->date))
		return false;

	if ($cart->get_items_total() < 0) {
		display_error("On line $line the invoice total amount cannot be less than zero.");
		return false;
	}
	return true;
}

/**
 * Validates the item part of one csv line of a sales order / invoice.
 *
 * $line_no is the line within the document, used in the notification only.
 */
function check_import_item_data(int $line_no, import_sales_line $row, Cart $cart): bool
{
	/** @var sys_prefs $SysPrefs */
	global $SysPrefs;

	$is_inventory_item = is_inventory_item($row->item_code);
	if ($row->item_description === '') {
		display_error( _("Item description cannot be empty."));
		return false;
	}
	elseif (!is_numeric($row->quantity) || !is_numeric($row->discountpercentage)) {
		display_error( _("The item could not be updated because you are attempting to set the quantity ordered to less than 0, or the discount percent to more than 100."));
		return false;
	} elseif (!is_numeric($row->price) && (!$SysPrefs->allow_negative_prices() || $is_inventory_item)) {
		display_error( _("Price for inventory item must be entered and can not be less than 0"));
		return false;
	}

	$price = (float)$row->price;
	$cost_home = get_unit_cost($row->item_code); // was get_standard_cost() before FA 2.4 // Added 2011-03-27 Joe Hunt
	$rate = get_exchange_rate_from_home_currency($cart->customer_currency, $cart->document_date);
	$cost = $rate != 0.0 ? $cost_home / $rate : $cost_home;
	if ($price < $cost)
	{
		$dec = user_price_dec();
		$curr = $cart->customer_currency;
		$price_text = number_format2($price, $dec);
		if ($cost_home == $cost)
			$std_cost = number_format2($cost_home, $dec);
		else
		{
			$price_text = $curr . " " . $price_text;
			$std_cost = $curr . " " . number_format2($cost, $dec);
		}
		display_notification_centered(sprintf(_("Item {$row->item_code}'s price on line $line_no of %s is below Standard Cost of %s"), $price_text, $std_cost));
	}

	return true;
}

/**
 * Copies the header columns of one csv line into the document being built.
 */
function copy_to_cart(Cart $cart, import_sales_line $row): void
{
	$cart->customer_id = $row->customer_id;
	$myrow = get_customer($row->customer_id);
	$cart->customer_name = (string)$myrow["name"];
	$cart->Branch = $row->branch_no;
	//sales_type eg. Retail, Wholesale id=1 or 2 respectively
	$cart->sales_type_name = $row->sales_type_name;
	$cart->sales_type = get_sales_type_id($row->sales_type_name);
	$cart->reference = $row->reference;
	$cart->document_date = $row->date;
	//initialise newpayment
	$newpayment = false;

	if ($row->payment_id !== '' && ($cart->payment != $row->payment_id)) {
		$cart->payment = $row->payment_id;
		$cart->payment_terms = get_payment_terms($row->payment_id);
		$newpayment = true;
	}

	if (import_payment_terms_flag($cart, 'cash_sale'))
	{
		if ($newpayment) {
			$cart->due_date = $cart->document_date;
			$cart->phone = $cart->cust_ref = $cart->delivery_address = '';
			$cart->ship_via = 0;
			$cart->deliver_to = '';
		}

	} else {
		$cart->due_date = $row->deldate;
		$cart->cust_ref = $row->custref;
		$cart->deliver_to = $row->delto;
		$cart->delivery_address = $row->deladdress;
		$cart->phone = $row->contactphone;
		$cart->ship_via = (int)$row->shipvia; // blank csv cell -> 0, strict MySQL rejects ''
	}

	$cart->Location = $row->delfrom;
	$cart->freight_cost = 0.0; // the freight column of the csv is not imported
	$cart->email = $row->email;
	if ($cart->trans_type != ST_SALESORDER && $cart->trans_type != ST_SALESQUOTE) { // 2008-11-12 Joe Hunt
		$cart->dimension_id = (int)$row->dimension_id; // blank csv cell -> 0, strict MySQL rejects ''
		$cart->dimension2_id = (int)$row->dimension2_id;
	}

	$cart->ex_rate = null; // the ex_rate column of the csv is not imported; FrontAccounting looks the rate up
	$cart->Comments = $row->comments;
}

/**
 * @return int id of the sales type, 0 when there is no sales type of that name
 */
function get_sales_type_id(string $sales_type_name): int
{
	$sql = "SELECT id FROM ".TB_PREF."sales_types WHERE sales_type=".db_escape($sales_type_name);

	$result = db_query($sql, "could not get sales type id");

	$row = db_fetch_row($result);
	return $row === false || $row === null ? 0 : (int)$row[0];
}

function customer_branch_exists(string $customer_id, string $branchNo): bool
{
	$sql = "SELECT * FROM ".TB_PREF."cust_branch WHERE branch_code=".db_escape($branchNo)." AND debtor_no = ".db_escape($customer_id);
	$result = db_query($sql, "could not query customer branch records.");
	return db_num_rows($result) > 0;
}
