<?php
/**
 * Typed representations of the CSV rows the importer reads.
 *
 * fgetcsv() hands back an untyped list of strings; everything downstream works with these value objects so that
 * each column has one known type and a missing column is an empty string instead of an undefined offset.
 */

/**
 * @param array<int, string|null> $data one CSV record
 */
function import_cell(array $data, int $index): string
{
	return isset($data[$index]) ? $data[$index] : '';
}

/**
 * A journal, deposit or payment line.
 */
final class import_line
{
	/** @var string */
	public $reference = '';
	/** @var string */
	public $date = '';
	/** @var string */
	public $memo = '';
	/** @var float */
	public $amt = 0.0;
	/** @var string amount written in the opposite direction column of a bank statement */
	public $ignore = '';
	/** @var string */
	public $code_id = '';
	/** @var int */
	public $taxtype = 0;
	/** @var string */
	public $dim1_ref = '';
	/** @var string */
	public $dim2_ref = '';
	/** @var string as typed in the csv; '' means the column was left blank */
	public $person_type_id = '';
	/** @var string as typed in the csv; '' means the column was left blank */
	public $person_id = '';
	/** @var string */
	public $branch_no = '';

	/**
	 * @param array<int, string|null> $data one CSV record
	 * @param int $type ST_JOURNAL, ST_BANKPAYMENT or ST_BANKDEPOSIT
	 * @param bool $statement true when the csv is a bank statement (both amount columns present)
	 */
	public static function fromCsv(array $data, int $type, bool $statement): self
	{
		$line = new self();
		$line->reference = import_cell($data, 0);
		$line->date = import_cell($data, 1);
		$line->memo = import_cell($data, 2);
		if ($type == ST_JOURNAL) {
			// reference, date, memo, amount, accountcode, taxtype, dim1ref, dim2ref, person_type_id, person_id
			$offset = 3;
			$line->amt = (float)import_cell($data, 3);
		} elseif ($statement) {
			// payments: reference, date, memo, payment, receipt, ...  deposits: reference, date, memo, payment, receipt, ...
			$offset = 4;
			if ($type == ST_BANKPAYMENT) {
				$line->amt = (float)import_cell($data, 3);
				$line->ignore = import_cell($data, 4);
			} else {
				$line->ignore = import_cell($data, 3);
				$line->amt = (float)import_cell($data, 4);
			}
		} else {
			// reference, date, description, amount, account, taxtype, dim1, dim2, person_type_id, person_id, branch_id
			$offset = 3;
			$line->amt = (float)import_cell($data, 3);
		}
		$line->code_id = import_cell($data, $offset + 1);
		$line->taxtype = (int)import_cell($data, $offset + 2);
		$line->dim1_ref = import_cell($data, $offset + 3);
		$line->dim2_ref = import_cell($data, $offset + 4);
		$line->person_type_id = import_cell($data, $offset + 5);
		$line->person_id = import_cell($data, $offset + 6);
		$line->branch_no = import_cell($data, $offset + 7);
		if ($type == ST_JOURNAL) {
			$line->memo = $line->memo . " Date: " . $line->date . " Reference: " . $line->reference;
		}
		return $line;
	}
}

/**
 * A sales order / sales invoice line.
 */
final class import_sales_line
{
	/** @var string */
	public $customer_id = '';
	/** @var string */
	public $branch_no = '';
	/** @var string */
	public $reference = '';
	/** @var string */
	public $date = '';
	/** @var string */
	public $payment_id = '';
	/** @var string */
	public $sales_type_name = '';
	/** @var string */
	public $dimension_id = '';
	/** @var string */
	public $dimension2_id = '';
	/** @var string */
	public $item_code = '';
	/** @var string */
	public $item_description = '';
	/** @var string */
	public $quantity = '';
	/** @var string */
	public $unit = '';
	/** @var string */
	public $price = '';
	/** @var string */
	public $discountpercentage = '';
	/** @var string */
	public $delfrom = '';
	/** @var string */
	public $deldate = '';
	/** @var string */
	public $delto = '';
	/** @var string */
	public $deladdress = '';
	/** @var string */
	public $contactphone = '';
	/** @var string */
	public $email = '';
	/** @var string */
	public $custref = '';
	/** @var string */
	public $shipvia = '';
	/** @var string */
	public $comments = '';
	/** @var string the csv also carries freightcost (col 15) and ex_rate (col 25); the importer has always ignored both */
	public $summary = '';

	/**
	 * @param array<int, string|null> $data one CSV record
	 */
	public static function fromCsv(array $data): self
	{
		$row = new self();
		$row->customer_id = import_cell($data, 0);
		$row->branch_no = import_cell($data, 1);
		$row->reference = import_cell($data, 2);
		$row->date = import_cell($data, 3);
		$row->payment_id = import_cell($data, 4);
		$row->sales_type_name = import_cell($data, 5);
		$row->dimension_id = import_cell($data, 6);
		$row->dimension2_id = import_cell($data, 7);
		$row->item_code = import_cell($data, 8);
		$row->item_description = import_cell($data, 9);
		$row->quantity = import_cell($data, 10);
		$row->unit = import_cell($data, 11);
		$row->price = import_cell($data, 12);
		$row->discountpercentage = import_cell($data, 13);
		$row->delfrom = import_cell($data, 15);
		$row->deldate = import_cell($data, 16);
		$row->delto = import_cell($data, 17);
		$row->deladdress = import_cell($data, 18);
		$row->contactphone = import_cell($data, 19);
		$row->email = import_cell($data, 20);
		$row->custref = import_cell($data, 21);
		$row->shipvia = import_cell($data, 22);
		$row->comments = import_cell($data, 23);
		$row->summary = implode(', ', array_map('strval', $data));
		return $row;
	}
}
