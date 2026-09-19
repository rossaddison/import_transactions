<?php
//module/import_transactions
include_once(__DIR__ . "/import_rows.inc");

/**
 * Name of the uploaded csv file, for the messages.
 */
function import_file_name(): string
{
    return isset($_FILES['imp']['name']) ? $_FILES['imp']['name'] : '';
}

/**
 * A posted form value as text; $default when the field was not posted (or was posted as an array).
 */
function import_post(string $key, string $default = ''): string
{
    if (isset($_POST[$key]) && is_scalar($_POST[$key])) {
        return $_POST[$key];
    }
    return $default;
}

/**
 * Result of splitting a tax inclusive amount.
 */
final class import_tax_split
{
    // amount without tax
    public float $net = 0.0;
    // amount as entered (rounded)
    public float $inclusive = 0.0;
    // inclusive amount minus tax
    public float $exclusive = 0.0;
    public float $tax = 0.0;
    // gl account that carries the tax of this amount (sales account for credits, purchasing account for debits)
    public string $taxaccount = '';
    public string $salestaxaccount = '';
    public string $purchasetaxaccount = '';
}

function all_delete(bool $yes = true): void
{
    if ($yes != true) {
        return;
    }
    display_notification(_("Function 'all_delete' has been set to true: Relevant records deleted before each test run."));

    // journal entries, bank entries and tax entries
    foreach (array('gl_trans', 'bank_trans', 'trans_tax_details') as $table) {
        import_empty_table($table);
    }

    //next_reference entries table: sys_types
    db_query("UPDATE ".TB_PREF."sys_types SET next_reference='1'", _("Could not update records."));
    display_notification(_("Reset column 'next_reference' in table 'sys_types' to '1' for all types."));

    // references, supplier transactions, customer transactions and the documents behind them
    foreach (array('refs', 'supp_trans', 'debtor_trans', 'debtor_trans_details', 'comments', 'sales_orders',
        'sales_order_details', 'stock_moves', 'cust_allocations') as $table) {
        import_empty_table($table);
    }
}

function import_empty_table(string $table): void
{
    $result = db_query("SELECT * FROM ".TB_PREF.$table, _("Could not query ").$table);
    $count = db_num_rows($result);
    if ($count > 0) {
        db_query("DELETE FROM ".TB_PREF.$table, _("Could not delete records."));
        display_notification(_("Deleted ").$count._(" records from table ").$table.".");
    }
}

/**
 * The person type FrontAccounting stores for a csv person_type_id.
 *
 * The csv convention is 1 = supplier, 2 = customer, blank = none; FrontAccounting 2.4 uses PT_SUPPLIER, PT_CUSTOMER and PT_MISC.
 *
 * @psalm-pure
 */
function fa_person_type(string $csv_type): int
{
    if ($csv_type == 1) {
        return PT_SUPPLIER;
    }
    if ($csv_type == 2) {
        return PT_CUSTOMER;
    }
    return PT_MISC;
}

function import_is_customer_line(import_line $l): bool
{
    return $l->person_type_id == 2 && customer_exist($l->person_id);
}

function import_is_supplier_line(import_line $l): bool
{
    return $l->person_type_id == 1 && supplier_exist($l->person_id);
}

/**
 * Adds one general ledger line of an imported bank transaction and puts it on the audit trail.
 */
function import_bank_gl(int $type, int $curEntryId, import_line $l, string $account, int $dim1, int $dim2, float $amount): void
{
    add_gl_trans($type, $curEntryId, $l->date, $account, $dim1, $dim2, $l->memo, $amount, null, fa_person_type($l->person_type_id), $l->person_id, "", 0);
    add_audit_trail($type, $curEntryId, $l->date);
}

// extra inclusive of tax column in csv
function bank_inclusive_tax(int $type, string $bank_account, string $bank_account_gl_code, int $line, int $curEntryId, import_line $l, int $dim1, int $dim2): void
{
    $split = determinetax($l->taxtype, $l->amt);
    $inclusive_amt = $split->inclusive;
    $exclusive_amt = $split->exclusive;
    $taxamt = $split->tax;
    $file = import_file_name();
    $code = $l->code_id;
    $date = $l->date;
    $memo = $l->memo;
    $ptype = fa_person_type($l->person_type_id);
    if ($type == ST_BANKDEPOSIT)
    {
        add_bank_trans($type, $curEntryId, $bank_account, $l->reference, $date, $inclusive_amt, $ptype, $l->person_id, "", "", 0);
        display_notification_centered(_("Added to table 'bank_trans': $curEntryId, $date, {$l->reference}, $code, $dim1, $dim2, $inclusive_amt (line $line in import file '$file')"));

        if ($l->taxtype > 0)
        {
            import_bank_gl($type, $curEntryId, $l, $code, $dim1, $dim2, -$exclusive_amt);
            display_notification_centered(_("Added to table 'gl_trans Credit:': $curEntryId, $date, $code, $dim1, $dim2, -$exclusive_amt, $memo (line $line in import file '$file')"));

            import_bank_gl($type, $curEntryId, $l, $split->salestaxaccount, $dim1, $dim2, -$taxamt);
            display_notification_centered(_("Added tax to table 'gl_trans Credit:': $curEntryId, $date, {$split->salestaxaccount}, $dim1, $dim2, -$taxamt, $memo (line $line in import file '$file')"));

            if (import_is_customer_line($l))
            {
                display_notification_centered(_("No tax types should be used with customer deposits since tax is accounted for at source document entry - eg. Invoice'. Use customer id with a control account only. (line $line in import file '$file')"));
            }

            if (import_is_supplier_line($l))
            {
                display_notification_centered(_("No tax types should be used with supplier payments since tax is accounted for at source document entry - eg. Invoice'. Use supplier id with a control account only.(line $line in import file '$file')"));
            }
        }

        if ($l->taxtype == 0)
        {
            import_bank_gl($type, $curEntryId, $l, $code, $dim1, $dim2, -$inclusive_amt);
            display_notification_centered(_("Added to table 'gl_trans Credit:': $curEntryId, $date, $code, $dim1, $dim2, -$inclusive_amt, $memo (line $line in import file '$file')"));

            if (import_is_customer_line($l))
            {
                write_customer_trans(ST_BANKDEPOSIT, $curEntryId, $l->person_id, $l->branch_no, $date, $l->reference, $inclusive_amt);
                display_notification_centered(_("Added to table 'debtor_trans Credit: Payment from customer': $inclusive_amt (line $line in import file '$file')"));
            }

            if (import_is_supplier_line($l))
            {
                write_supp_trans(ST_BANKDEPOSIT, $curEntryId, $l->person_id, $date, "", $l->reference, "", $inclusive_amt, 0, 0);
                display_notification_centered(_("Added to table 'supp_trans Credit: Our over-payment amount reimbursed': $inclusive_amt (line $line in import file '$file')"));
            }
        }

        import_bank_gl($type, $curEntryId, $l, $bank_account_gl_code, $dim1, $dim2, $inclusive_amt);
        display_notification_centered(_("Added to table 'gl_trans' Debit bank account: $curEntryId, $date, $bank_account_gl_code, $dim1, $dim2, $inclusive_amt, $memo (line $line in import file '$file')"));
        add_gl_tax_details($split->salestaxaccount, ST_BANKDEPOSIT, $curEntryId, -$taxamt, 1, $date, $memo, 1, -$inclusive_amt);
    }
    if ($type == ST_BANKPAYMENT)
    {
        add_bank_trans($type, $curEntryId, $bank_account, $l->reference, $date, -$inclusive_amt, $ptype, $l->person_id, "", "", 0);
        display_notification_centered(_("Added to table 'bank_trans': $curEntryId, $date, {$l->reference}, $code, $dim1, $dim2, -$inclusive_amt (line $line in import file '$file')"));

        if ($l->taxtype > 0)
        {
            import_bank_gl($type, $curEntryId, $l, $code, $dim1, $dim2, $exclusive_amt);
            display_notification_centered(_("Added to table 'gl_trans' Debit: $curEntryId, $date, $code, $dim1, $dim2, $exclusive_amt, $memo (line $line in import file '$file')"));

            import_bank_gl($type, $curEntryId, $l, $split->purchasetaxaccount, $dim1, $dim2, $taxamt);
            display_notification_centered(_("Added tax to table 'gl_trans' Debit: $curEntryId, $date, {$split->purchasetaxaccount}, $dim1, $dim2, $taxamt, $memo (line $line in import file '$file')"));

            if (import_is_customer_line($l))
            {
                display_notification_centered(_("No tax types should be used with customer deposits since tax is accounted for at source document entry - Invoice'. Use customer id with a control account only. (line $line in import file '$file')"));
            }

            if (import_is_supplier_line($l))
            {
                display_notification_centered(_("No tax types should be used with supplier payments since tax is accounted for at source document entry - Invoice'. Use supplier id with a control account only.(line $line in import file '$file')"));
            }
        }
        if ($l->taxtype == 0)
        {
            import_bank_gl($type, $curEntryId, $l, $code, $dim1, $dim2, $inclusive_amt);
            display_notification_centered(_("Added to table 'gl_trans' Debit: $curEntryId, $date, $code, $dim1, $dim2, $inclusive_amt, $memo (line $line in import file '$file')"));

            if (import_is_customer_line($l))
            {
                write_customer_trans(ST_BANKPAYMENT, $curEntryId, $l->person_id, $l->branch_no, $date, $l->reference, -$inclusive_amt);
                display_notification_centered(_("Added to table 'debtor_trans Debit:' Customer over-payment reimbursed -$inclusive_amt (line $line in import file '$file')"));
            }

            if (import_is_supplier_line($l))
            {
                write_supp_trans(ST_BANKPAYMENT, $curEntryId, $l->person_id, $date, "", $l->reference, "", -$inclusive_amt, 0, 0);
                display_notification_centered(_("Added to table 'supp_trans' Debit: Supplier paid -$inclusive_amt (line $line in import file '$file')"));
            }
        }
        import_bank_gl($type, $curEntryId, $l, $bank_account_gl_code, $dim1, $dim2, -$inclusive_amt);
        display_notification_centered(_("Added to table 'gl_trans' Credit bank account: $curEntryId, $date, $bank_account_gl_code, $dim1, $dim2, -$inclusive_amt, $memo (line $line in import file '$file')"));
        add_gl_tax_details($split->purchasetaxaccount, ST_BANKPAYMENT, $curEntryId, $taxamt, 1, $date, $memo, 1, $inclusive_amt);
    }
}

//notify when tax type used with income or expenses.
//issue warning if 'ie' is returned false => tax type used with asset or liability.
function check_tax_appropriate(string $code, int $taxtype, int $line): bool
{
    $ie = true;
    if ($taxtype > 0)
    {//get account_code and account_type from chart master
        $file = import_file_name();
        $sql = "SELECT account_code,account_type FROM ".TB_PREF."chart_master WHERE account_code = ".db_escape($code);
        $result = db_query($sql, _("Could not query chart_master table"));
        $row = db_fetch($result);
        $acctype = $row === false ? '' : (string)$row['account_type'];
        $sql2 = "SELECT id, class_id FROM ".TB_PREF."chart_types WHERE id = ".db_escape($acctype);
        $result2 = db_query($sql2, _("Could not query chart_types table"));
        $row2 = db_fetch($result2);
        $classid = $row2 === false ? 0 : (int)$row2['class_id'];

        //if classid < 3 => asset or liability
        if ($classid > 2) {
            $ie = true;
            display_notification_centered(_("Correct: Tax type used with Income or expense:$code. (line $line in import file '$file')"));
        } else {
            $ie = false;
            display_notification_centered(_("Warning: Taxtype used with Asset or Liability.(line $line in import file '$file')"));
        }
    }
    return $ie;
}

function check_code_id(string $code): bool
{
    $sql = "SELECT account_code FROM ".TB_PREF."chart_master WHERE account_code = ".db_escape($code);
    $result = db_query($sql, _("Could not query chart_master table"));
    return db_num_rows($result) > 0;
}

/**
 * Checks that the customer or supplier of a csv line exists and that control accounts are used with a person.
 * A found customer or supplier is added to the memo of the line.
 *
 * @return bool true when the line has an error
 */
function check_customer_supplier(import_line $l, int $line, bool $error): bool
{
    $file = import_file_name();
    $debtors_act = import_post('debtors_act');
    $creditors_act = import_post('creditors_act');
    $code_id = $l->code_id;
    $person_id = $l->person_id;
    if ($person_id !== '' && $person_id !== '0')
    {
        //check that debtor or supplier exist
        if (($code_id == $debtors_act) && ($l->person_type_id == 2) && !customer_exist($person_id)) {
            display_notification_centered(_("Error: Customer does not exist. (line $line in import file '$file') "));
            $error = true;
        }
        if (($code_id == $creditors_act) && ($l->person_type_id == 1) && !supplier_exist($person_id)) {
            display_notification_centered(_("Error: Supplier does not exist. (line $line in import file '$file') "));
            $error = true;
        }
        //build a memo on current memo if entry line acceptable
        if (($code_id == $debtors_act) && ($l->person_type_id == 2) && customer_exist($person_id))
        {
            $l->memo = $l->memo." Customer name: ".customer_name($person_id)." id: ".$person_id;
        }
        if (($code_id == $creditors_act) && ($l->person_type_id == 1) && supplier_exist($person_id))
        {
            $l->memo = $l->memo." Supplier name: ".supplier_name($person_id)." id: ".$person_id;
        }
    }
    //check for default control accounts used without $person_id and $person_type_id
    if (($code_id == $debtors_act) && (($person_id === '') || ($l->person_type_id === ''))) {
        display_notification_centered(_("Error: Debtors Control account used without person_type_id or person_id. (line $line in import file '$file') "));
        $error = true;
    }
    if (($code_id == $creditors_act) && (($person_id === '') || ($l->person_type_id === ''))) {
        display_notification_centered(_("Error: Creditors Control account used without person_type_id or person_id. (line $line in import file '$file') "));
        $error = true;
    }

    return $error;
}

function customer_exist(string $person_id): bool
{
    $sql = "SELECT debtor_no FROM ".TB_PREF."debtors_master WHERE debtor_no=".db_escape($person_id);
    $result = db_query($sql, _("Could not query debtors master table"));
    return db_num_rows($result) > 0;
}

function customer_name(string $person_id): string
{
    $sql = "SELECT name FROM ".TB_PREF."debtors_master WHERE debtor_no=".db_escape($person_id);
    $result = db_query($sql, _("Could not query debtors master table"));
    $row = db_fetch($result);
    return $row === false ? '' : (string)$row['name'];
}

function supplier_exist(string $person_id): bool
{
    $sql = "SELECT supplier_id FROM ".TB_PREF."suppliers WHERE supplier_id=".db_escape($person_id);
    $result = db_query($sql, _("Could not query supplier master table"));
    return db_num_rows($result) > 0;
}

function supplier_name(string $person_id): string
{
    $sql = "SELECT supp_name FROM ".TB_PREF."suppliers WHERE supplier_id=".db_escape($person_id);
    $result = db_query($sql, _("could not query supplier master table"));
    $row = db_fetch($result);
    return $row === false ? '' : (string)$row['supp_name'];
}

//if taxtype > 0 separate into inclusive, exclusive, and tax in preparation for gl split
function determinetax(int $taxtype, float $amt): import_tax_split
{
    $sql = "SELECT * FROM ".TB_PREF."tax_types WHERE id=".db_escape($taxtype);
    $result = db_query($sql, _("Could not query tax type table"));
    $row = db_fetch($result);
    $split = new import_tax_split();
    // an unknown tax type (0 = no tax) is treated as untaxed
    $taxper = $row === false ? 0.0 : (float)$row['rate'];
    //determine the VAT accounts associated with the taxtype eg. standard VAT
    $split->salestaxaccount = $row === false ? '' : (string)$row['sales_gl_code'];
    $split->purchasetaxaccount = $row === false ? '' : (string)$row['purchasing_gl_code'];
    $split->tax = round(($amt/(1.0+($taxper/100.0))*($taxper/100.0)),2);
    $split->taxaccount = $split->tax < 0 ? $split->salestaxaccount : $split->purchasetaxaccount;
    $split->inclusive = round($amt,2);
    //get the exclusive value
    $split->net = round($amt/(1.0+($taxper/100.0)),2);
    //avoid rounding issues with the following line
    $split->exclusive = $split->inclusive - $split->tax;
    return $split;
}

/**
 * Shows the collected journal, deposit or payment lines as a table.
 *
 * @return bool true when at least one line was shown
 */
function display_import_items(string $title, items_cart $order): bool //adapted from display_gl_items
{
    $displayed_at_least_once = false;
    display_heading($title);
    $dim = (int)get_company_pref('use_dimension');
    div_start('items_table');
    start_table(TABLESTYLE2, "colspan=7 width=95%");
    if ($dim == 2) {
        $th = array(_("Account Code"), _("Account Description"), _("Dimension")." 1",_("Dimension")." 2", _("Debit"), _("Credit"), _("Memo"),_("Jnl Entry"));
    } elseif ($dim == 1) {
        $th = array(_("Account Code"), _("Account Description"), _("Dimension"),_("Debit"), _("Credit"), _("Memo"),_("Jnl Entry"));
    } else {
        $th = array(_("Account Code"), _("Account Description"),_("Debit"), _("Credit"), _("Memo"), _("Jnl Entry"));
    }

    if (count($order->gl_items) > 0) {
        $th[] = '';
    }
    table_header($th);
    $k = 0;
    $num = 0;
    foreach ($order->gl_items as $item)
    {
        $num = $num + 1;
        alt_table_row_color($k);
        label_cell($item->code_id, "width = 10%");
        label_cell($item->description,"width = 30%");
        if ($dim >= 1) {
            label_cell(get_dimension_string($item->dimension_id, true),"wrap width = 10%");
        }
        if ($dim > 1) {
            label_cell(get_dimension_string($item->dimension2_id, true),"wrap width = 10%");
        }
        if ($item->amount > 0) {
            amount_cell(abs($item->amount),false,"width=10%");
            label_cell("","width = 10%");
        } else {
            label_cell("","width = 10%");
            amount_cell(abs($item->amount),false,"width=10%");
        }
        //note: the $item->reference is carrying $memo for display purposes.
        label_cell($item->reference,"wrap align=left width=20%");
        label_cell($num,"width=5% align=right colspan=3");
        end_row();
    }
    if ($order->count_gl_items() > 0) {
        $colspan = ($dim == 2 ? "4" : ($dim == 1 ? "3" : "2"));
        start_row();
        label_cell(_("Total"), "align=right colspan=" . $colspan);
        amount_cell($order->gl_items_total_debit());
        amount_cell(abs($order->gl_items_total_credit()));
        label_cell('', "colspan=4");
        end_row();
        $displayed_at_least_once = true;
    }
    end_table();
    echo "<br>";
    div_end();
    return $displayed_at_least_once;
}

function display_entries(int $type, items_cart $entry): bool
{
    if ($type == 1) {
        $title = "Payment Processing";
    } elseif ($type == 2) {
        $title = "Deposit Processing";
    } else {
        $title = "General Journal Processing";
    }
    return display_import_items(_($title), $entry);
}

/**
 * @return int|null the dimension id, 0 for a blank reference, null when there is no dimension with that reference
 */
function get_dimension_id_from_dimref(string $ref): ?int
{
    if (trim($ref) == '') {
        return 0;
    }

    $sql = "SELECT id FROM ".TB_PREF."dimensions WHERE reference LIKE ".db_escape($ref);

    $result = db_query($sql, _("could not get dimension from reference"));

    $row = db_fetch_row($result);

    return $row === false || $row === null ? null : (int)$row[0];
}

//$entry or item_cart class merely used for tabular display of journal entries.
function init_entry_part_1(items_cart $entry): void
{
    $entry->clear_items();
}

function init_entry_part_2(items_cart $entry, string $date, string $reference): void
{
    $entry->tran_date = $date;
    $entry->reference = $reference;
    $entry->memo_ = 'Imported via \'Import Multiple Journal Entries\' plugin';
}

/**
 * Adds the lines of one csv line to the table shown for a journal, deposit or payment.
 *
 * @return int the number of the next gl item
 */
function journal_display(int $i, int $type, int $taxtype, float $amt, items_cart $entry, string $code_id, int $dim1, int $dim2, string $memo, string $description, string $bank_account_gl_code, string $bankdesc): int
{
    if ($taxtype > 0) //tax inclusive
    {
        $split = determinetax($taxtype, $amt);
        $taxdesc = get_gl_account_name($split->taxaccount);
        if ($type == 0) //tax inclusive and journal line
        {
            $entry->gl_items[$i] = new gl_item($code_id, $dim1, $dim2, $split->exclusive, $memo, $description);
            $i = $i + 1;
            $entry->gl_items[$i] = new gl_item($split->taxaccount, $dim1, $dim2, $split->tax, $memo, $taxdesc);
        }

        if ($type == 1) //tax inclusive and bank payment
        {
            $entry->gl_items[$i] = new gl_item($code_id, $dim1, $dim2, $split->exclusive, $memo, $description);
            $i = $i + 1;
            $entry->gl_items[$i] = new gl_item($split->taxaccount, $dim1, $dim2, $split->tax, $memo, $taxdesc);
            $i = $i + 1;
            $entry->gl_items[$i] = new gl_item($bank_account_gl_code, $dim1, $dim2, -$amt, $memo, $bankdesc);
        }

        if ($type == 2) //tax inclusive and bank deposit
        {
            $entry->gl_items[$i] = new gl_item($code_id, $dim1, $dim2, -$split->exclusive, $memo, $description);
            $i = $i + 1;
            $entry->gl_items[$i] = new gl_item($split->taxaccount, $dim1, $dim2, -$split->tax, $memo, $taxdesc);
            $i = $i + 1;
            $entry->gl_items[$i] = new gl_item($bank_account_gl_code, $dim1, $dim2, $amt, $memo, $bankdesc);
        }
    }

    if ($taxtype == 0)
    {
        if ($type == 0) //tax exclusive and journal line
        {
            $entry->gl_items[$i] = new gl_item($code_id, $dim1, $dim2, $amt, $memo, $description);
        }

        if ($type == 1) //tax exclusive and bank payment
        {
            $entry->gl_items[$i] = new gl_item($code_id, $dim1, $dim2, $amt, $memo, $description);
            $i = $i + 1;
            $entry->gl_items[$i] = new gl_item($bank_account_gl_code, $dim1, $dim2, -$amt, $memo, $bankdesc);
        }

        if ($type == 2) //tax exclusive and bank deposit
        {
            $entry->gl_items[$i] = new gl_item($code_id, $dim1, $dim2, -$amt, $memo, $description);
            $i = $i + 1;
            $entry->gl_items[$i] = new gl_item($bank_account_gl_code, $dim1, $dim2, $amt, $memo, $bankdesc);
        }
    }

    return $i + 1;
}

// extra inclusive of tax column in csv
function journal_inclusive_tax(int $type, int $line, int $curEntryId, import_line $l, int $dim1, int $dim2): void
{
    $split = determinetax($l->taxtype, $l->amt);
    $file = import_file_name();
    $date = $l->date;
    $memo = $l->memo;
    $ptype = fa_person_type($l->person_type_id);
    if ($l->taxtype > 0) //amt inclusive of tax
    {
        add_gl_trans($type, $curEntryId, $date, $split->taxaccount, $dim1, $dim2, $memo, $split->tax, null, $ptype, $l->person_id, "", 0); //credit tax control account
        if ($split->net < 0) {
            display_notification_centered(_("Added tax to table 'gl_trans' Credit: $curEntryId, $date, {$split->taxaccount}, $dim1, $dim2, {$split->tax}, $memo (line $line in import file '$file')"));
        }
        if ($split->net > 0) {
            display_notification_centered(_("Added tax to table 'gl_trans' Debit: $curEntryId,$date, {$split->taxaccount}, $dim1, $dim2, {$split->tax}, $memo (line $line in import file '$file')"));
        }
        add_gl_tax_details($split->taxaccount, ST_JOURNAL, $curEntryId, $split->tax, 1, $date, $memo, 1, $split->inclusive);
    }
    add_gl_trans($type, $curEntryId, $date, $l->code_id, $dim1, $dim2, $memo, $split->exclusive, null, $ptype, $l->person_id, "", 0);
    if ($split->net > 0) {
        display_notification_centered(_("Added to table 'gl_trans' Debit: $curEntryId, $date, {$l->code_id}, $dim1, $dim2, {$split->exclusive}, $memo (line $line in import file '$file')"));
    }
    if ($split->net < 0) {
        display_notification_centered(_("Added to table 'gl_trans' Credit: $curEntryId, $date, {$l->code_id}, $dim1, $dim2, {$split->exclusive}, $memo (line $line in import file '$file')"));
    }
}

/**
 * Tracks when the csv lines of a journal add up to debits equal to credits.
 *
 * As soon as debits equal credits the journal is complete and the next csv line starts a new one. Every line of a
 * journal must carry the same date as the line before it.
 *
 * @return array{0: bool, 1: bool, 2: float, 3: float} error, journal complete, total debits, total credits
 */
function journal_id(?string $prev_date, string $date, float $amt, float $total_debit_positive, float $total_credit_negative, int $line): array
{
    $error = false;
    $complete = false;
    if ($amt > 0) {
        $total_debit_positive = $total_debit_positive + $amt;
    }
    if ($amt < 0) {
        $total_credit_negative = $total_credit_negative + abs($amt);
    }
    if (round($total_debit_positive - $total_credit_negative, 2) == 0)
    {
        if ($prev_date != $date) {
            display_notification(_("Error: Dates do not correspond. (line $line in import file '".import_file_name()."')"));
            $error = true;
        }

        $total_debit_positive = 0.0;
        $total_credit_negative = 0.0;
        $complete = true;
    }
    return array($error, $complete, $total_debit_positive, $total_credit_negative);
}

// The highest transaction number already used by this type of transaction. FrontAccounting numbers every type
// separately, so the number must be looked up per type (get_next_trans_no() also checks voided transactions).
// Reading the last row of bank_trans or gl_trans instead picks up another type's number and collides with an existing
// transaction as soon as payments, deposits and transfers have been entered in a different order.
function last_transno(int $type): int
{
    if ($type == ST_BANKDEPOSIT || $type == ST_BANKPAYMENT || $type == ST_JOURNAL
        || $type == ST_SALESORDER || $type == ST_SALESINVOICE)
    {
        return (int)get_next_trans_no($type) - 1;
    }
    return 0;
}
